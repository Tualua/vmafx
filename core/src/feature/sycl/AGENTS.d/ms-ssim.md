---
paths:
  - core/src/feature/sycl/integer_ms_ssim_sycl.cpp
  - core/test/test_sycl_ms_ssim_parity.c
invariant: integer_ms_ssim_sycl.cpp = CPU arithmetic, type for type; honours enable_chroma, enable_lcs, enable_db.
---
<!-- markdownlint-disable MD013 MD060 -->
# Multi-Scale SSIM extractor and kernels

- **`integer_ms_ssim_sycl.cpp` honours `enable_chroma` option parity**
  (ADR-0526, ADR-0583). `enable_chroma`
  option (default `false`) clamps `n_planes` to 1 in `init_fex_sycl` when
  set to `false`, to 3 otherwise (except YUV400P which always forces 1).
  Chroma geometry uses the picture allocator's ceil subsampling, so a
  176x176 chroma minimum maps to an exact 351x351 4:2:0 luma minimum;
  the init error suggestion uses that exact inverse. Submit, computation and
  publication iterate every active plane, so `enable_chroma=true` dispatches
  Y, Cb and Cr today. On rebase: keep the default, YUV400P clamp and
  three-plane dispatch aligned with the CPU and Metal MS-SSIM extractors.
- **`integer_ms_ssim_sycl.cpp` honours `enable_lcs`, `enable_db`,
  `clip_db` GPU option parity** (ADR-0243, ADR-1078). When
  `enable_lcs=true`, emits 15 extra metrics
  (`float_ms_ssim_{l,c,s}_scale{0..4}`). When `enable_db=true`,
  returns `-10*log10(1 - ms_ssim)` instead of raw linear score;
  `clip_db=true` derives the geometry-dependent `max_db` ceiling from frame
  dimensions and bit depth, then caps the dB-domain output at that ceiling
  (ADR-1221). It never clamps the linear score to `[0, 1]`.
  All three options default to `false` — output at default settings
  numerically identical to pre-ADR-1078 binary. Metric ordering
  and `places=4` cross-backend contract = part of public API
  surface. See
  [../../AGENTS.md §"MS-SSIM `enable_lcs` GPU contract"](../../../AGENTS.md).
- **`integer_ssim_sycl.cpp` and `integer_ms_ssim_sycl.cpp` are
  self-contained submit/collect** — do **not** register with
  `vmaf_sycl_graph_register`. `integer_ms_ssim_sycl.cpp` needs float
  [0, 255] intermediates from `picture_copy()`; `float_ssim_sycl` uploads
  its own raw luma and does that scaling on the device (ADR-1370).
  `ciede_sycl` TU follows same pattern. **On rebase**: do not
  "consolidate" these into graph register — precision posture
  load-bearing. Reading the shared frame from `float_ssim_sycl` is a
  separate decision (it would also serve `vmaf_read_pictures_sycl()`).
- **`picture_copy()` channel parameter** — `integer_ms_ssim_sycl.cpp`
  passes `channel=0` per d3647c73 prerequisite port
  (`integer_ssim_sycl.cpp` no longer calls `picture_copy()`, ADR-1370). See
  [../../AGENTS.md §"`picture_copy()` carries a `channel`
  parameter"](../../../AGENTS.md).
- **`integer_ms_ssim_sycl.cpp` waits once per frame (ADR-1363).** Each
  (plane, scale) owns the span `partial_offset[plane][scale]` of
  `d_partials` / `h_partials` (`[l x groups][c x groups][s x groups]`, int64
  since ADR-1414); `submit()` enqueues the pyramid and every scale's
  `enqueue_scale_lcs` plus one copy of the whole buffer, and `collect()`
  waits once and sums each span (`sum_scale_lcs`). **On rebase**: do not
  share one partials buffer across scales again (the reuse is what forced
  the per-scale wait); the horizontal workspace may be shared because the
  queue is in order.
- **`integer_ms_ssim_sycl.cpp` = CPU arithmetic, type for type (ADR-1414).**
  Four things, each one a regression if undone:
  (1) `decimate_pixel()`: every tap `sycl::fma(sample, tap, acc)`, rows
  first, then the nine row sums = `ms_ssim_decimate.c` (`vmaf_fmaf_exact`).
  Plain `acc += sample * tap` is NOT contracted in this TU (ADR-1367) ->
  1e-6 off. (2) Window sums: `add_horizontal_tap` / `add_vertical_tap` /
  `round_moments` from `sycl_ssim_terms.h` = `iqa_convolve()`'s fp32
  products summed in fp64, as fp32 pairs, one rounding per pass. (3)
  `ssim_terms()`: fp32 variances + clamp, fp32 denominators, l and c as
  pairs (CPU fp64 quotients), s = `div_rn` fp32 quotient, C3 = C2 / 2.0f.
  (4) Sums: `term_fixed()` -> int64 units of 2^-52, `reduce_over_group`
  exact, host `FixedSum`, mean `(double)(float)(sum / pixels)` (=
  `iqa_ssim()` float return), `combine_ms_ssim()` with `fabs()` on l, c, s.
  `sycl_ssim_terms.h` is shared with `float_ssim_sycl`
  (`integer_ssim_sycl.cpp`): ONE copy of the arithmetic, no private
  `ssim_terms` / `add_*_tap` / `term_fixed` in either TU (contract test).
  Header holds host-only `double` (`FixedSum::value`); never call it from a
  kernel (ADR-0220). Exact in practice, not by construction: pairs ~2^-46
  vs CPU fp64, exact sum vs CPU running fp64 sum; fp32 mean rounding absorbs
  it. Arc A380: every per-scale mean identical on Netflix pair (48),
  checkerboards (3 + 3), BBB 4K (50), chroma, 10 / 12 / 16 bit. Score vs a
  GCC build: 1 of 254 frames 1.1e-16 off = host `pow()` of the icx build
  (libimf), not the device. Same-binary CPU on AVX-512 host needs #1706.
  Scratch-free (ADR-1395). `EXACT_TWINS`: `float_ms_ssim`,
  `float_ms_ssim_lcs`: `sycl`. Guards: `test_sycl_ms_ssim_parity` (+
  `_large`; `==` on 18 outputs x 3 frames, runs FIRST in the binary so its
  scalar-CPU cpumask precedes the process-wide SSIM dispatch install),
  `test_sycl_kernel_source_contract.py` (seven planted regressions).

- [ADR-0243](../../../../../docs/adr/0243-enable-lcs-gpu.md) — MS-SSIM
  `enable_lcs` GPU contract.

| SYCL TU | CPU TU | Parity test | ADR |
|---|---|---|---|
| `integer_ms_ssim_sycl.cpp` | `ms_ssim.c` | `test_sycl_ms_ssim_parity.c` (+ `_large`; bit-exact, 18 outputs x 3 frames) | ADR-0884 (round 2), ADR-1414 |

| Kernel TU | Parity test | ADR |
|---|---|---|
| `integer_ms_ssim_sycl.cpp` | `test_sycl_ms_ssim_parity.c` | ADR-0884 |
