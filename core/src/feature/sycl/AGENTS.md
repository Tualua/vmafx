<!-- markdownlint-disable MD060 -->
# AGENTS.md — core/src/feature/sycl

Orientation for agents on per-feature SYCL kernels (DPC++).
Parent: [../AGENTS.md](../AGENTS.md). Backend runtime (queue, USM,
dmabuf import) lives one level up in
[`../../sycl/AGENTS.md`](../../sycl/AGENTS.md).

## Scope

```text
feature/sycl/
  <feature>_sycl.cpp           # one TU per kernel: registration + submit/collect + sycl::queue::submit lambda
```

All TUs compiled with `icpx` (Intel oneAPI) — build line
under [`../../meson.build`](../../meson.build) adds `-fsycl` and the SYCL
strict FP line (`sycl_strict_fp_args`, ADR-1367) for every per-kernel TU.

## Ground rules

- **Parent rules** apply (see [../AGENTS.md](../AGENTS.md) +
  [../../AGENTS.md](../../AGENTS.md) +
  [`../../sycl/AGENTS.md`](../../sycl/AGENTS.md)).
- **Every TU is strict-clean regardless of origin or age.** A file ported from
  Netflix, copied from another backend, or present before the ratchet has no
  warning exemption. Its oneAPI compile, SYCL clang-tidy projection, cppcheck,
  and HISS audit must report no file-local diagnostic when touched. Split
  oversized helpers at cohesive phase boundaries; do not add `NOLINT`, lower a
  baseline, or filter a diagnostic to make a lane green. Regenerate the SYCL
  database with `scripts/ci/gen-sycl-compile-commands.py` before clang-tidy.
- **SYCL strict FP line load-bearing, one line for every TU
  ([ADR-1367](../../../../docs/adr/1367-sycl-strict-fp-every-feature-tu.md)).**
  `sycl_strict_fp_args` = `-fp-model=precise -ffp-contract=off
  -foffload-fp32-prec-div -foffload-fp32-prec-sqrt`, in that order.
  precise alone leaves `a * b + c` contracted into an FMA inside kernel
  lambdas and fp32 `/` and `sqrt` approximate (ADR-1358); without precise,
  icpx's fast model drifts `float_adm_sycl` past `places=4` (ADR-0202).
  With the line, a kernel's `+ - * / sqrt` round like the CPU reference's;
  transcendentals (`sycl::log2`, `exp`, `pow`, `cbrt`), reduction order and
  fp32 stand-ins for fp64 reference expressions still differ. Where a
  kernel approximates an fp64 reference expression, write the fused form it
  needs as `sycl::fma()` (`integer_vif_sycl.cpp` `sv_sq`); never rely on
  contraction. No TU gets a private FP list; the precision pair also rides
  `sycl_dependency` to the link for the SPIR-V JIT image (MSVC: the explicit
  device link, ADR-1364, takes the whole line).
  `test_sycl_fp_arith_contract` checks the device result on hardware.
- **SpEED pipeline arithmetic contract ([ADR-1358](../../../../docs/adr/1358-sycl-speed-device-resident-linalg.md)).**
  Every SpEED kernel lives in `speed_sycl_pipeline.cpp`; the two
  extractor TUs and `speed_sycl_host.cpp` hold none and never wait on
  the queue outside `pipeline_collect()` / `pipeline_wait()`. The four
  TUs are built with contraction off like every feature TU
  (`sycl_strict_fp_args`, ADR-1367). In the
  pipeline, every division and square root goes through `div_rn()` /
  `sqrt_rn()` (shared with ssimulacra2 in `sycl_exact_fp.h`, ADR-1363),
  every `log2f` through
  `speed_log2()`, every product feeding an add sits in a named
  temporary, and the fp64 comparisons of `speed.c` go through
  `below_eps()` / `below_eps_scaled()`. The file must not mention the
  fp64 type at all (`core/test/test_sycl_kernel_source_contract.py`).
  `lanczos4` prescale weights = host table, never a device sine: CPU
  rounds each weight once from fp64 `sin()`, `sycl::sinpi()` is ulps off
  and SpEED amplifies (5.8e-5 relative on an A380,
  `T-GPU-SPEED-LANCZOS4-PRESCALE-DRIFT-2026-09-30`). `upload_lanczos()`
  fills `Pipeline::lanczos` (device USM) at init from
  `speed_internal_gpu_lanczos_weights()`; `scale_lanczos()` reads `wx` /
  `wy` from it, so no private `wx[9]` / `wy[9]` (scratch, ADR-1395).
  Contract test plants a `sycl::sinpi`, a missing table, a private array;
  `test_sycl_speed_lanczos4_parity` = device check.
  On rebase: a plain `/` or `sycl::sqrt` added to a pipeline kernel, or
  a reduction reordered, breaks the bit-exact parity
  `test_sycl_speed_*_parity` measures; keep the order of every sum
  identical to its `speed.c` / `vif_tools.c` reference.
- **fp64-free kernels non-negotiable** ([ADR-0220](../../../../docs/adr/0220-sycl-fp64-fallback.md)).
  Every SYCL feature-kernel lambda captures, operates on `float`
  / integer types only. **No `double` operand inside `parallel_for`
  body**, no `sycl::reduction<double>`, no `sycl::plus<double>`.
  Hard rule, not soft: single fp64 instruction anywhere in
  TU's SPIR-V module causes Level Zero runtime to reject
  entire module on Intel Arc A-series and other fp64-less devices —
  even when offending kernel never submitted.
  - `double` allowed **outside** kernel lambda — host-side
    post-processing in `extract` / `flush` callbacks, score
    aggregation, log10 normalisation.
  - ADM gain limiting: `adm_gain_limit_product()` (`../adm_gain_limit.h`,
    64-bit integer only) = the CPU's truncated double product, exact for
    every limit in [1, 100] (ADR-1413); limit split on the host by
    `adm_gain_limit_split()`.
  - VIF gain limiting uses fp32 `sycl::fmin`.
- **No scratch memory in kernels ([ADR-1395](../../../../docs/adr/1395-sycl-kernels-no-scratch.md)).**
  No private array indexed at run time outside local memory, no live set above
  128 registers per thread at kernel SIMD width: Arc A-series under xe returns
  wrong values from scratch. Kernel that cannot fit -> functor derived from
  `VmafSyclKernelShape<SG, 256>` (`sycl_compat.h`, 256-entry register file).
  `integer_vif_sycl.cpp` SIMD-32 hori + fused kernels need it (spilled up to
  8832 B/thread without); do not turn them back into plain lambdas or drop the
  shape. `VMAF_SYCL_VIF_SUBGROUP_SIZE=32` reaches them on Intel GPUs;
  `test_sycl_vif_parity_sg32` runs parity through them. Run
  `test_sycl_kernel_scratch` on Intel GPU after any kernel change;
  `core/src/sycl/scratch_ratchet.txt` is empty and stays empty (no kernel
  left with scratch since 2026-10-01). Smallest trap: private array indexed
  by a run-time value, e.g. `pixel.original[band]` in the old
  `float_adm_sycl` CM kernels (896 B private, NaN on A380 under xe) ->
  select by value: every helper of `sycl_float_adm_math.h` takes its band as
  a constant, `Bands` holds the CSF weights as three named fields.
- **Kernel identities and output captures have an explicit boundary**
  ([Research-2090](../../../../docs/research/2090-sycl-silent-revert-residuals-2026-09-24.md)).
  Anonymous kernel lambdas in two translation units can receive identical
  generated names, letting the linker pair one launcher's host capture layout
  with the other's device image; that is how the two SpEED TUs collided. Since
  ADR-1358 every SpEED kernel lives in the one TU `speed_sycl_pipeline.cpp`, and
  the source contract rejects a kernel in `speed_chroma_sycl.cpp`,
  `speed_temporal_sycl.cpp` or `speed_sycl_host.cpp`. Do not split the pipeline
  kernels back across TUs.
  `float_psnr_sycl.cpp` and `integer_psnr_sycl.cpp` capture their output
  pointers through `FpsnrOutput` and `PsnrKernelArgs`; do not flatten those
  structs back into raw lambda captures. `integer_moment_sycl.cpp` is the
  remaining scalar-argument shape and aliases `d_sums` to `e_sums` before the
  submit lambda. Keep the alias and use it for all four atomics. The source
  contract in `core/test/test_sycl_kernel_source_contract.py` plants the fp64,
  SpEED host-residual, kernel-outside-pipeline, mid-frame-wait and raw-capture
  regressions and must stay wired into the fast suite.
- **Wholly-new fork files use dual Netflix + Lusoris/Claude
  copyright header** per [ADR-0025](../../../../docs/adr/0025-copyright-handling-dual-notice.md).
  Most TUs here fork-original SYCL ports of
  Netflix CUDA kernels.

## Twin-update rules

When a SYCL TU has a live CUDA, HIP, or Metal twin, user-visible behavior and
numeric fixes must be reviewed across those twins in the same PR. Vulkan was
removed in ADR-0726 and is not a live twin. The complete CUDA mapping lives in
[`../cuda/AGENTS.md`](../cuda/AGENTS.md). The cross-backend parity gate at
`places=4`
([`scripts/ci/cross_backend_parity_gate.py`](../../../../scripts/ci/cross_backend_parity_gate.py),
ADR-0214) catches drift only after a full GPU run; it does not replace that
source review.

## Parity invariant — motion3 CPU and SYCL moving-average paths

`integer_motion.c` (CPU) and `integer_motion_sycl.cpp` (SYCL) both implement
motion3 post-process as host-side moving average over blended motion2
scores. Both paths **must stay in numerical parity at places=4** (delta
≤ 1e-4, per ADR-0214). Gate enforced by
`core/test/test_sycl_motion3_parity.c`. Any change to blend formula
(`motion_blend()`), moving-average guard condition, or `motion_max_val`
clipping must mirror across both files. Same for CUDA / Vulkan /
HIP / Metal motion twins listed in Twin-update table above — same PR.

## Rebase-sensitive invariants

- **A failed extractor `init` owns its cleanup (BUG-048 section E).** The
  generic feature-extractor framework does not invoke `close` after `init`
  returns an error. Every SYCL init path that has acquired USM, a feature-name
  dictionary, or a graph registration must therefore call its NULL-safe local
  close callback before propagating the error. This is enforced without a GPU
  by `core/test/test_sycl_init_unwind.cpp`; keep the allocator, dictionary, and
  graph fault cases when rebasing any init/close pair. Historical producer
  `709ce470e` was reverted by `5d070b0b4`; the current restoration boundary is
  documented in
  `docs/research/2101-bug048-sycl-init-unwind-restoration-2026-09-24.md`.

- **`integer_motion_sycl.cpp::motion3_postprocess_*` honours
  motion3 GPU contract** (ADR-0219). Applies CPU's host-side
  post-process to motion2 with no device-side state.
  `motion_five_frame_window=true` returns `-ENOTSUP` at `init()` with
  `WARNING` log. See [../../AGENTS.md §"motion3_score GPU contract"](../../AGENTS.md).

- **Motion SAD = one shared kernel, difference first
  (T-SYCL-MOTION-TINY-FRAME-PARITY-2026-09-29).** `motion_sycl` and
  `motion_v2_sycl` both call `motion_sycl_pipeline::enqueue_sad()`
  (`integer_motion_pipeline_sycl.{h,cpp}`): `sum |blur(prev - cur)|`,
  vertical pass rounded `>> bpc`, horizontal `>> 16`, reflect-101 borders =
  CPU `integer_motion.c` / `integer_motion_v2.c` `motion_score_pipeline_*`
  since the Netflix a4a1492d port (PR #532). Bit-exact; gate
  `test_sycl_motion_tiny_frames` compares with `==` (3x3 .. 1283x723, 8/10/16
  bit). `blur(cur) - blur(prev)` rounds twice per pixel -> 2e-4 off at 17x17;
  never reintroduce it. `prev - cur` order load-bearing (arithmetic shift
  floors negatives). Vertical sum int32 up to 15 bpc, int64 at 16 (host picks
  the `submit_sad<Acc>` instance). Kernel lives only in the pipeline TU
  (Research-2090 name collision); extractor TUs hold none. `motion_sycl` keeps
  the raw luma of the previous frame in `d_raw_y[2]` (device memcpy from the
  shared frame after the kernel), because the shared frame buffers are
  overwritten by the next upload. Measured cost vs the old per-frame blur
  (4K micro-benchmark, kernel + copy): about +11% on B580 and UHD 770.

- **`integer_motion_sycl.cpp::motion_add_uv` GPU contract** (ADR-0989).
  When `motion_add_uv=true`, `submit_fex_sycl` packs the reference U and V
  planes into pinned host staging (`h_stage_u` / `h_stage_v`);
  `motion_pre_graph` copies them H2D on the combined queue into
  `d_ref_u[cur_slot]` / `d_ref_v[cur_slot]`; `enqueue_motion_work` runs the
  shared SAD kernel on `d_ref_*[1 - cur_slot]` - `d_ref_*[cur_slot]`,
  accumulating into `d_sad_u` / `d_sad_v`.
  `collect_fex_sycl` sums Y + U + V contributions, each normalized by
  respective plane area (`chroma_w × chroma_h` for UV in YUV420P). The
  numerical gate is the scalar fixed-point oracle in
  `test_sycl_motion_add_uv_parity.c` (ADR-1326), including its required
  960x540 variant. `float_motion(motion_add_uv=true)` has the same semantic
  option but different coefficients and float reduction order, so it is not
  the kernel's numerical oracle.
  CUDA, Vulkan, HIP, and Metal twins expose option but return
  `-ENOTSUP` with `WARNING` until their kernel ports land. On rebase:
  if upstream Netflix adds `motion_add_uv` to `integer_motion.c`, verify
  per-plane normalization formula stays consistent.
  **Queue-sync invariant (T-SYCL-MOTION-ADD-UV-SUBMIT-WAIT-2026-09-29,
  supersedes the ADR-1034 primary-queue wait)**: no host wait in `submit()`.
  UV H2D rides the in-order combined queue in `pre_fn`, ahead of the kernels
  (graph replay is fenced by `ext_oneapi_submit_barrier()`). Staging is
  safe to refill in the next `submit()`: the graph fires on the last
  extractor's submit, after staging, and this extractor's `collect()` of the
  previous frame (`vmaf_sycl_graph_wait`) drained the copy that read it. Do
  not move the UV copies back to `vmaf_sycl_memcpy_h2d_async` (primary queue)
  — that needs the host wait again.

- **`integer_vif_sycl.cpp` rd_stride uses ceiling division for odd widths** (ADR-1034).
  Both `launch_vif_hori_impl` (scalar/SIMD-32) and `launch_vif_fused_impl` (SIMD-16)
  compute downsampled row stride as `(e_w + 1U) / 2U`, not `e_w / 2U`.
  `rd_ref`/`rd_dis` allocation in `init_fex_sycl` uses `((w+1U)/2U) * ((h+1U)/2U)`
  elements. Must stay in sync. On rebase: if future PR modifies
  downsampling path, ensure all three sites (two kernel variants + allocation) use
  same ceiling formula. For even widths/heights result identical to
  truncating division.
  **Reader side too** (T-SYCL-VIF-ODD-WIDTH-RD-STRIDE-2026-09-29):
  `enqueue_vif_work_impl` passes scale s > 0 the stride `(prev_w + 1U) / 2U`
  it was written with, while `cur_w` stays `prev_w / 2` (CPU floor). Reading
  at `cur_w` skewed scales 1-3 on any odd-width scale (17x17 scale1 0.0962 vs
  CPU 0.0765; 854x480 scale3 9.3e-4 off). Guard: `test_sycl_vif_min_dim`
  (17x17, 853x480 at places=4).

- **`integer_vif_sycl.cpp` = CPU `vif`, bit for bit (ADR-1432).** Two parts.
  (1) Host tail = `integer_vif.c` rounding points: `vif_store_residuals()`
  stores each scale's num / den in `float`, `write_scores()` adds the
  ROUNDED values for debug sums, emitter divides single precision
  (`.single_precision_ratio = true`). Twin: `vif_scale_sums()` -> two
  `(float)(...)` casts per scale, `vif_score_set()` mirrors
  `write_scores()`. `debug` default = `false` like CPU. (2) Gain terms:
  CPU forms `g = sigma12 / (sigma1_sq + eps)`, `sigma2_sq - g * sigma12`,
  `g * g * sigma1_sq` in fp64, truncates two integers. Kernel calls
  `vmaf_sycl_ivif::gain_terms()` (`sycl_integer_vif_math.h`): ONE integer
  division `sigma12^2 / sigma1_sq` (`divide()`: two fp32 estimates +
  integer fix, no 64-bit divider), `eps` as the fp64 sum rounds it
  (`divisor_of()`), zones = fp64 chain's own error (2^-20 for `sv_sq`,
  2^-51 relative for the product); undecided sample (1 pixel in 300 000)
  -> `gain_terms_replayed()` = reference's six fp64 ops on `SoftDouble`
  (`sycl_soft_double.h`, shared with `float_vif`). NEVER an fp32 gain
  again: it left numerators 1-6 fp32 steps off (3.6e-7). Integer limit:
  `g < L` <=> `sigma12 <= L * sigma1_sq`, product exact; non-integer
  limit at the limit -> replay. NO `sycl::mul_hi()` on uint64: wrong
  values in a kernel on Arc A380 (icpx 2026.0); `u128_mul()` in 32-bit
  limbs. Scratch (ADR-1395): per-pixel terms = `vif_terms` (seven
  `int32_t`, widened in `dev_reduce_and_accum()`), fused scale-0 kernel
  takes 256-entry register file at SIMD-16 too (`vif_fused_grf_size()`;
  default file spilled 128 B). Arc A380: Netflix 48 / 48, checkerboards
  3 / 3, BBB 4K 200 / 200, 10 / 12 / 16 bit, `debug=true`, limits 1.0 /
  1.2 / 37.5, clip vs itself: identical. Cost 21.46 -> 22.21 ms / 4K
  frame. `integer_vif.c` gain lines change upstream -> change header same
  PR. Guards: `test_sycl_integer_vif_math` (host + device vs the fp64
  lines), `test_sycl_vif_parity` (+ `_large`, `_sg32`, `==`),
  `test_sycl_vif_exact_gain_contract.py` (seven planted regressions),
  `test_sycl_vif_float_sums_contract.py`, `test_sycl_kernel_scratch`.

- **`integer_vif_sycl.cpp` minimum frame = 16 px, declared through ADR-1324**
  (T-INTEGER-VIF-TINY-FRAME-GUARD-2026-09-29, maintainer decision: CPU
  fallback). `VIF_MIN_DIM` = max over scales of `(half_width + 1) << s` for
  `vif_fwidth` / `vif_fwidth_rd` (`static_assert` 16): below it a consumed tap
  sits more than one reflection outside the plane -> device lost.
  `.context_check` returns -ENOTSUP below it, `.context_fallback_name = "vif"`
  -> model dispatch (and CLI twin selection) runs the CPU `vif`; direct
  `vif_sycl` fails `init()` with -EINVAL before touching device state. On
  rebase: filter-table change -> update the `static_assert`, keep both guards.

- **`integer_vif_sycl.cpp` warning-clean phase boundaries are load-bearing.**
  Keep `dev_vert_accumulate`, `dev_hori_item_step`, `vif_init_resources`,
  `vif_configure_device`, and `vif_register_graph` as bounded phases. The
  strict C++ profile requires private declarations in anonymous namespaces,
  while HISS-04 applies its 60-line limit to each namespace block as well as
  each function; do not collapse these blocks or replace them with `NOLINT`.
  The tap loops deliberately have no forced-unroll pragma: oneAPI 2026 emits a
  failed-unroll diagnostic for supported target instances and remains free to
  unroll them when profitable. Preserve the `float` device gain in
  `VifHoriLaunchParams`, the arithmetic order inside each phase, and the
  cleanup points in the three init helpers. Base-vs-refactor proof covers
  default and fused modes on 8-bit and 10-bit inputs at zero full-precision
  delta; rerun both modes after an upstream conflict.

- **`integer_psnr_sycl.cpp` honours `enable_chroma` option parity**
  (ADR-0453). `enable_chroma` option (default `true`) clamps `n_planes`
  to 1 in `init_fex_sycl` when set to `false`, matching CPU
  `integer_psnr.c::init`'s behaviour. On rebase: keep clamp and
  `default_val.b = true` aligned with CUDA and Vulkan twins; all three
  backends must agree on default and dispatch logic.

- **`integer_psnr_sycl.cpp` = full CPU `psnr` option table, bit-exact**
  ([ADR-1365](../../../../docs/adr/1365-sycl-twin-cpu-option-parity.md)).
  Device reduces per-plane SSE only. `enable_mse`, `enable_apsnr`,
  `reduced_hbd_peak`, `min_sse`, `uncapped` act on host through
  `feature/psnr_score.h` (`vmaf_psnr_peak` / `_max` / `_from_mse` /
  `_aggregate`) — same helpers as CPU `integer_psnr.c`. `emit_plane()`
  order = CPU (`psnr_*`, then `mse_*`); `collect()` folds SSE + sample
  count into `apsnr_*` totals, `flush_fex_sycl()` publishes aggregates
  after final collect. **On rebase**: no local copy of PSNR math; keep
  option table = CPU table (names, defaults, range, no `FEATURE_PARAM`).
  Guard: `test_sycl_twin_option_parity`.

- **`integer_ssim_sycl.cpp` SSIM twins take CPU options; identical window
  = exactly 1** (ADR-1365). `integer_ssim_sycl`: `enable_db`, `clip_db`;
  `float_ssim_sycl`: `enable_lcs`, `enable_db`, `clip_db`, `scale`. dB
  conversion + ceiling on host (`vmaf_ssim_max_db`,
  `vmaf_ssim_emit_*_named` in `nonfinite_score.h`). Per-window formula:
  every product in a named temporary, variances summed as a pair,
  `numerator == denominator ? 1 : n / d`. Mirrored operation sequence ->
  identical window gives exactly 1 -> `enable_db` = CPU's `+inf` /
  `clip_db` ceiling (ADR-1221), not finite dB of an fp32 residue.
  `enable_lcs` = separate kernel `launch_vert_combine_lcs` (default path
  keeps one reduction), `float_ssim_lcs()` = `iqa/ssim_tools.c` L/C/S in
  fp32 (clamped variances, flat-window covariance clamp, C3 = C2 / 2),
  partials `[l | c | s]`. Since ADR-1414 the per-pixel helpers
  (`ssim_terms`, `add_*_tap`, `term_fixed`, `FixedSum`) live in
  `sycl_ssim_terms.h`, shared with the MS-SSIM twin.
  **On rebase**: do not fold products back into
  expressions (icpx contracts `a * b + c`, ADR-1358) or restore the
  left-to-right four-term variance sum; the identical-frame cases in
  `test_sycl_twin_option_parity` fail on either.

- **`integer_ssim_sycl` = CPU `ssim`, bit for bit (ADR-1443).** Supersedes
  the fp32 formula of the bullet above for the fixed-point twin
  (`float_ssim_sycl` unchanged). CPU: `ssim_reduce_row_range()` forms each
  pixel's term in fp64 from int64 moments, `calc_ssim()` adds every term
  into ONE double in raster order. Twin: pass 1 = five int64 horizontal
  moment planes (no weight plane: weight = product of the two tap sums,
  `tap_weight(tap_range())`; frame weight sum = `line_weight(w) *
  line_weight(h)` on host). Pass 2 = `IssimTermKernel`: vertical moments,
  then `vmaf_sycl_issim::term_bits()` (`sycl_integer_ssim_math.h`) = the
  reference's fp64 operations one for one, in its order, on
  `SoftSigned` (sign, 53-bit significand, exponent in integers,
  `sycl_soft_signed.h`; RN ties-to-even per operation). Stores the fp64
  BIT PATTERN per pixel (`uint64_t`), NO reduction on device. Host:
  `integer_ssim_frame_sum()` adds the plane in index order. Three
  shortcuts, each exact: (a) window weight 2^16 (every window inside the
  frame) -> product with it = exponent + 16 (`times_weight()`), edge
  windows take the multiplication; (b) all six integer products below
  2^52 (always at 8 / 10 bit) -> the four product sums are integers
  below 2^53, no rounding (`product_sums_exact()`), else
  `product_sums_rounded()` = each product rounded, sums left to right;
  (c) quotient = three radix-2^19 digits, digit estimated by fp32 division
  of the top 24 bits, remainder exact in int64, two corrections each way
  (`soft_div_digits()`): device division accuracy does not matter.
  NEVER: an fp32 term (3.1e-7 on 4K, overflow -> `invalid ratio` at 16
  bit), a per-group reduction (1.1e-11 even in double), `k * (w * w)` for
  c1 / c2 (reference rounds twice), `w * (a * b)`. Zero has no sign in
  `SoftSigned` (terms go into a sum). Shape: SIMD-16 + 256-entry register
  file (`ISSIM_TERM_SG`, `ISSIM_TERM_GRF`); SIMD-16 default file = 3072 B
  private + 1248 B spill, SIMD-32 spills at both -> wrong values on xe
  (ADR-1395). Term
  function `flatten, always_inline`, header functions
  `VMAF_SYCL_ALWAYS_INLINE`: a call left in the kernel = scratch frame.
  Arc A380: Netflix 48 / 48, checkerboards 3 / 3, BBB 4K 200 / 200, 10 /
  12 / 16 bit, 4:2:2, `enable_db` / `clip_db`: identical (vs GCC CPU:
  dB differs 3.6e-15 on 10 of 266 frames = host `log10`, libimf vs glibc).
  Cost 17.8 -> 31.9 ms / 4K frame (arithmetic 7.2, read-back 5.8, host
  adds 2.8): `T-SYCL-SSIM-EXACT-THROUGHPUT-2026-10-02`.
  `ssim_reduce_row_range()` lines change upstream -> change the header
  same PR. Guards: `test_sycl_integer_ssim_math` (operations + term vs
  fp64, host + device), `test_sycl_ssim_parity` (+ `_large`, `==`, 15
  cases, 12 fail on the old twin), `test_sycl_ssim_exact_contract.py`
  (13 planted regressions), `test_sycl_kernel_scratch`.

- **`float_motion_sycl.cpp` emits through `motion_clip()`** (ADR-1365).
  Every emitted `motion` / `motion2` (debug score, tail in `flush()`
  included) = `MIN(score * motion_fps_weight, motion_max_val)`, CPU
  `float_motion.c` order (min of two SADs first). `motion_force_zero`
  short-circuits `submit()` (no upload, no kernel) and `collect()` emits
  zeros; before ADR-1365 it was declared and ignored. `motion3` not
  provided (CPU extractor only).

- **`float_motion_sycl.cpp` SAD = CPU order, bit for bit (ADR-1409,
  ADR-1411).** `float_motion.c::compute_motion_simd()` = one fp32 running
  sum per row, one fp32 sum over rows, fp32 division. Twin:
  `launch_float_motion_row_sad()` = ONE work-item per row
  (`sycl::range<1>(height)`, sub-group size 8), `fm_row_sad()` = plain
  `for (j = 0; j < width; j++)` loop, readback `height` floats; host =
  `vmaf_float_motion_score_from_row_sads()` (`../float_motion_sad.h`). No
  group / sub-group / atomic reduction in the TU, no host sum in
  `collect()`: any other shape differs in low bits (was 1.36e-4 on 1080p
  checkerboards). Blur kernel writes blur only; blur =
  `convolution_f32_c_s()` tap order, needs the strict FP line (ADR-1367).
  CPU SAD order changes upstream -> change kernel + helper in same PR.
  Arc A380: Netflix pair 48 / 48, checkerboards 3 / 3, BBB 4K 200 / 200
  identical (`motion`, `motion2`), also 10 / 12 / 16 bit. Cost: row pass
  re-reads both blurred planes, 0.70 ms per 4K frame (old reduction 0.34):
  3.85 -> 4.23 ms / frame. Sub-group 8 measured fastest (16: 0.82, 32:
  1.12); `select_from_group` chain over 16 consecutive pixels 1.10,
  lane-uniform 16-wide chain 0.80: do not retry without a new idea.
  Scratch-free (ADR-1395). `EXACT_TWINS` lists `float_motion`: `sycl`.
  Guards: `test_sycl_float_motion_parity` (+ `_large`, `==`, 8 / 10 / 12
  bit), `test_sycl_kernel_source_contract.py` (five planted regressions).

- **`float_vif_sycl.cpp` = CPU arithmetic, bit for bit (ADR-1412,
  ADR-1422).** Four CPU properties copied: (1) taps =
  `vif_get_filter()` on host (`init_vif_taps()`), passed BY VALUE
  (`VifTaps`), indexed only by unrolled-loop constants (run-time index ->
  private memory); no tap literal in the TU. (2) `log2` =
  `log2_approx()` (`sycl_float_vif_math.h`), CPU `log2f_approx()` op for
  op; never `sycl::log2`. (3) CPU's two fp64 expressions
  (`1 + g*g*s1 / (sv + nsq)`, `1 + s1 / nsq`; `vif_sigma_nsq` is
  `double`) = `one_plus_ratio()`: exact fp32 pair (`ff_div`, `ff_add`),
  and within 2^-12 step of a rounding boundary (1 sample in 1650) the
  fp64 ops replayed in int64 (`SoftDouble`). Pair alone wrong on 85 of
  8.4e9 quotients (ties: reference rounds twice): do NOT drop replay.
  Struct selects (`cond ? a : b` on `SoftDouble`) = 512 B private memory:
  select fields. (4) sums: `vif_row_sums()` ONE work-item per row
  (`sycl::range<1>(height)`, sub-group 8), plain left-to-right loop;
  host `sum_vif_rows()` adds rows in fp32. No group / sub-group / atomic
  reduction in the TU. Per scale: filter kernel (stores `sigma1_sq`,
  `sigma2_sq`, `sigma12`) -> `FloatVifStatisticKernel` (one work-item per
  pixel, sub-group 16, default register file; replaces first two planes
  by num / den terms) -> row sums; one readback per frame. Statistic
  inside the filter kernel spills (1440-1600 B) at default register file;
  with large register file scratch-free but 29.4 ms / 4K frame vs 24.1:
  do not merge back. Statistic at sub-group 32 default register file
  spills (480-640 B). Arc A380: Netflix pair 48 / 48, checkerboards 3 / 3,
  BBB 4K 200 / 200, 10 / 12 / 16 bit, `debug=true`, non-default options:
  identical. Cost: 20.54 -> 23.95 ms / 4K frame, +100 MB device memory
  (`T-SYCL-FLOAT-VIF-EXACT-THROUGHPUT-2026-10-01`). `vif_get_filter()`,
  `VIF_OPT_FAST_LOG2` / `log2f_approx()`, `vif_pixel_statistic_s()`,
  `vif_statistic_s()` change upstream -> change header same PR.
  `EXACT_TWINS` lists `float_vif`: `sycl`. Guards:
  `test_sycl_float_vif_math` (host + device vs `vif_statistic_s()`, fp64
  expressions, twelve witnesses), `test_sycl_float_vif_parity` (+
  `_large`, `==`, cases in `float_vif_twin_parity.h`),
  `test_sycl_float_vif_exact_contract.py` (eleven planted regressions),
  `test_sycl_kernel_scratch`.

- **`integer_psnr_sycl.cpp` uses ceiling division for chroma plane geometry**
  (PR #878 Vulkan twin fix). `cw` and `ch` computed via
  `(w + 1U) >> 1` / `(h + 1U) >> 1`, not `w / 2U` / `h / 2U`, to match
  CPU + CUDA + Vulkan behaviour on odd-dimension YUV420. On rebase: if
  upstream Netflix changes chroma-dimension formula in
  `integer_psnr.c::init`, propagate here and to CUDA and Vulkan twins
  in same PR.

- **`integer_psnr_hvs_sycl.cpp` DCT lives in local memory, never in one
  work-item's private arrays** (T-SYCL-PSNR-HVS-B580-SIGSEGV-2026-09-29,
  Research-2123). Old shape: work-item 0 ran the whole 8x8 DCT + masking on
  five private 64-element arrays; IGC 2.41.5 SIGSEGVs the host compiling that
  at SIMD32 for Xe2 (Arc B580). Now (ADR-1369, Research-1369): two work-items
  per block (2k ref, 2k + 1 dist, same sub-group), each with a 65-word local
  slot (pad = distinct banks): `hvs_load_block()` raw samples ->
  `hvs_variance_ratio()` -> `hvs_fdct8x8()` in place (columns stored as
  columns = CPU `z` transposed, then rows) -> `hvs_mask_energy()` ->
  `hvs_threshold()`; `permute_group_by_xor(.., 1)` exchanges the two
  thresholds, `group_barrier(sg)`, ref item runs `hvs_store_terms()`. One
  dispatch for all planes, terms laid out `[Y | Cb | Cr]` (`first_block[]`,
  64 floats per block). On rebase: no private `int[64]` / `float[64]`; do
  not "fix" by pinning `VMAF_SYCL_REQD_SG_SIZE(16)` (on the UHD 770 the
  compiler's own choice beats forced SIMD16 by 1.7x). Samples are read raw
  at every depth (the old `sample_to_int` x16 for 9/11 bits is gone).
  Guards: `test_sycl_psnr_hvs_parity{,_simd32,_large}`,
  `test_sycl_shared_planes`.

- **`psnr_hvs_sycl` = CPU scores bit for bit (ADR-1397, ADR-1401).** Kernel
  stores the 64 terms `calc_psnrhvs()` sums per block (`hvs_store_terms`,
  row-major, blocks in plane then raster order); `reduce_hvs_planes()` hands
  each plane to `vmaf_psnr_hvs_plane_score()` (`../psnr_hvs_score.c`: one
  running `float`, CPU order), combined score + dB via the same file.
  Load-bearing, each one breaks bit-identity on its own (Research-1401):
  - masking table = `(csf * 0.3885746225901003)^2` in `double`, stored
    `float` (`hvs_mask_value` / `MASK_TABLES`, constexpr: host compiler
    evaluates it, kernel reads static data, stays fp64-free);
  - threshold = `sqrt_prod_rn(energy, ratio) / 32.f`: fp32 rounding of the
    root of the exact product = the CPU's double product and root. Float
    product + `sycl::sqrt` = up to 4.4e-7 dB off on 3 of 48 frames;
  - coefficient error = integer `sycl::abs()` cast to `float`;
  - strict FP line (ADR-1367): no contraction, correctly rounded `/`
    (`threshold / mask`, variance ratio);
  - no sum of terms in the TU (per-block partials round differently:
    1e-2 dB at 3840x2160).
  Kernel stays scratch-free (ADR-1395): `private_mem_size` and
  `spill_memory_size` 0 on the A380 at SIMD16 and forced SIMD32; terms go
  straight to USM, no private `float[64]`; `test_sycl_kernel_scratch` fails
  if the kernel gains any. `n_active_planes` = 1 for `enable_chroma=false`
  or 4:0:0 (CPU `psnr_hvs.c::init` rule, set in `configure_hvs_geometry()`),
  else 3; 4:0:0 must not reach the shared chroma planes. Guards:
  `test_sycl_psnr_hvs_parity{,_simd32,_large}` (device, `==` on all four
  outputs, 3840x2160 included), `test_sycl_fp_arith_contract`
  (`sqrt_prod_rn` on the device vs the host's fp64 expression),
  `test_psnr_hvs_twin_exact_sum_contract.py` (device-free). Gate cell =
  tolerance 0 (`EXACT_TWINS`). Upstream change to `calc_psnrhvs()`
  arithmetic or order -> mirror it here in the same PR. Readback = 256 bytes
  per block (65 MB per 3840x2160 4:2:0 frame); tuning tracked as
  T-SYCL-HIP-PSNR-HVS-EXACT-SUM-THROUGHPUT-2026-10-01, must stay bit-exact.

- **`sycl_exact_fp.h::sqrt_prod_rn(a, b)`** (ADR-1401) = fp32-rounded root
  of the exact product, i.e. the host's `(float)sqrt((fp64)a * b)`, with no
  fp64: significands multiplied as `uint64_t` (48 bits), `isqrt_floor50()`
  (25 integer steps), odd floor root rounds up (no tie possible). Exact for
  normal positive operands only; anything else falls to `sqrt_rn(a * b)`.
  Do not replace the integer root by a device `sqrt` of the converted
  product: a third of all operand pairs round differently.

- **`integer_motion_v2_sycl.cpp` reads the shared frame** (ADR-1369). `cur`
  = `vmaf_sycl_get_shared_plane(state, 1, 0)` behind
  `vmaf_sycl_queue_after_upload()`; the ADR-1371 pipeline's `cur_copy` keeps it
  in `d_pix[index % 2]` as the next frame's `prev` (`enqueue_copy` on frame 0).
  No host copy, no private upload; the kernel stays in
  `integer_motion_pipeline_sycl.cpp`.

- **`integer_psnr_sycl.cpp` SSE: group reduction, 32-bit squares**
  (ADR-1369). `launch_sse()` = 16 pixels per work-item one grid apart,
  `reduce_over_group`, one atomic per group; `psnr_item_sse<uint32_t>` only
  while 16 squares fit (bpc <= 12). Chroma from the shared planes.

- **Tile loaders clamp after reflecting (`sycl_tile_index.h`)**
  (T-SYCL-TILE-HALO-OOB-READ-2026-09-29, Research-2123). Fixed-size SLM tiles
  load padding lanes too; one reflection of those leaves the plane on small
  frames (ADM vertical DWT: row 16 of an 8-row plane -> -1) and reads outside
  the USM buffer -> `UR_RESULT_ERROR_DEVICE_LOST` when the page is unmapped.
  Every single-reflection loader wraps the reflected index in
  `vmaf_sycl_tile_index()`: `integer_adm` `launch_dwt_vert_pair`, `integer_vif`
  `dev_vert_load_tile` + `dev_fused_load_tile`, `integer_motion_pipeline`
  `load_diff` (motion + motion_v2), `float_motion`
  `fm_load_tile`, `float_vif` `load_vif_tile`. Identity for consumed samples ->
  no score change. New tiled kernel = same wrap. Per-output reflections
  (`dev_hori_convolve_border`, float VIF decimate) are consumed-only and stay
  unwrapped; each extractor's minimum frame size keeps them in the plane
  (integer VIF: `VIF_MIN_DIM` above). Guards: `test_sycl_adm_tiny_frames`,
  `test_sycl_vif_min_dim`.

- **`integer_psnr_hvs_sycl.cpp` uses ceiling division for chroma plane
  geometry** (PR #1031). `init_fex_sycl` computes 4:2:0 / 4:2:2 chroma
  `width[1..2]` / `height[1..2]` via `(w + 1U) >> 1` / `(h + 1U) >> 1`, not
  `w >> 1` / `h >> 1`, to match `picture.c` / CPU `integer_psnr_hvs.c` /
  CUDA + HIP twins on odd-dimension YUV420 / YUV422. Floor division drops
  last chroma 8x8 block strip on odd dimensions, diverging `psnr_hvs_cb` /
  `psnr_hvs_cr` / `psnr_hvs` from every other backend (even dimensions
  unaffected). On rebase: picture allocator's ceiling subsample convention
  (`(dim + ss) >> ss`) = single source of truth. Any new SYCL extractor
  re-deriving plane dims in its own `init` must use ceiling form. Any
  upstream change to chroma-dimension formula propagates here and to
  CUDA + HIP twins in same PR.

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
  [../../AGENTS.md §"MS-SSIM `enable_lcs` GPU contract"](../../AGENTS.md).

- **`integer_ssim_sycl.cpp` and `integer_ms_ssim_sycl.cpp` are
  self-contained submit/collect** — do **not** register with
  `vmaf_sycl_graph_register`. `integer_ms_ssim_sycl.cpp` needs float
  [0, 255] intermediates from `picture_copy()`; `float_ssim_sycl` uploads
  its own raw luma and does that scaling on the device (ADR-1370).
  `ciede_sycl` TU follows same pattern. **On rebase**: do not
  "consolidate" these into graph register — precision posture
  load-bearing. Reading the shared frame from `float_ssim_sycl` is a
  separate decision (it would also serve `vmaf_read_pictures_sycl()`).

- **`float_ssim_sycl` decimation is bit-exact with the CPU**
  ([ADR-1370](../../../../docs/adr/1370-sycl-float-ssim-device-decimation.md)).
  `decimate_sample()` = `iqa_filter_pixel()` at `(x * scale, y * scale)`:
  window rows / columns `r - scale / 2` for `r` in `[0, scale)`,
  `symmetric_index()` = `KBND_SYMMETRIC`, product `sample * tap_weight`
  in fp32 (`tap_weight` = `ssim.c`'s `1.0f / (scale * scale)`, computed on
  the host), summed exactly in int64 units of 2^-52, converted once with
  `rounding_mode::rte`. No fp32 accumulation, no FMA path (no adds on
  floats), no reordering matters because the sum is integer. Exact only up
  to `SSIM_MAX_EXACT_SCALE` (128); `float_ssim_geometry_supported()` is
  the single predicate for `check_context_sycl()` and init, also requiring
  an 11x11 decimated plane. Plane size = `iqa_decimate_dim()` from
  `iqa/decimate_dim.h` (shared with `iqa/decimate.c`; include-free so this
  C++ TU never parses `convolve.h`). Samples: `picture_copy()`'s layout
  (uint16 at 10 / 12 / 16 bits scaled by the exact reciprocal of 4 / 16 /
  256, else uint8). Frame means go through `float_ssim_frame_mean()`,
  which rounds to fp32 like `iqa_ssim()`: removing it breaks `enable_db`
  on near-identical frames. One queue wait per frame, in `collect()`.
  Guards: `test_sycl_float_ssim_parity` (+ `_large`, scales 1-10, 8 / 10 /
  12-bit, odd sizes, `enable_lcs`, gate verdicts),
  `test_gpu_float_ssim_auto_scale_contract`. On rebase: a change to
  `iqa_filter_pixel()`, `KBND_SYMMETRIC`, `ssim_low_pass_alloc()` or
  `picture_copy()` scaling changes this kernel in the same PR.

- **`integer_ciede_sycl.cpp` stages Y/U/V at native size; kernel
  subsamples chroma** ([Research-2120](../../../../docs/research/2120-sycl-ciede-throughput.md)).
  `stage_plane()` packs each plane into host USM (`plane_w[p]` x
  `plane_h[p]`, chroma by `picture.c`'s ceil rule `(w + ss) >> ss`),
  one DMA per plane. `ciede_pixel()` reads chroma at
  `(x >> ss_hor, y >> ss_ver)` = nearest-neighbour upsample of
  `ciede.c::scale_chroma_planes`: horizontal from `ss_hor`, vertical
  from `ss_ver` (fork's fixed flags, not upstream's transposed pair).
  Same indexing as CUDA / HIP twins. **On rebase**: do not restore the
  host `upscale_plane` (9.5 of 15 ms per 4K frame on Arc B580) and do
  not floor chroma dims. `test_sycl_ciede_parity` pins odd 4:2:0,
  4:2:2 10-bit, 4:4:4 against CPU (cases in
  `core/test/ciede_twin_parity.h`).

- **`integer_ciede_sycl.cpp` = `ciede.c`'s statements on fp32 pairs
  ([ADR-1436](../../../../docs/adr/1436-sycl-ciede-cpu-arithmetic.md),
  after ADR-1426 for CUDA).** Arithmetic = `../ciede_ff_math.h` + pair
  functions `../ff_math.h`, shared with `ciede_hip` (ADR-1448):
  backend-neutral, no `sycl::` in them. `sycl_ff_math.h` /
  `sycl_ciede_math.h` = SYCL primitives only (`vmaf_ffm_base` =
  `vmaf_sycl_exact`, `VMAF_FF_*` macros -> `sycl::fabs` / `rint` / `sqrt` /
  `cbrt` / `pow(x, 0.2f)` / `ldexp`, `VMAF_FF_INLINE` =
  `VMAF_SYCL_ALWAYS_INLINE`) + namespace aliases `vmaf_sycl_ffm` /
  `vmaf_sycl_ciede`; no function definitions there. A change to a shared
  header changes both twins: A380 AND gfx1036 parity before merge (move
  measured bit-identical on 178 A380 frames). `ciede_ff_math.h` mirrors
  `../cuda/integer_ciede/ciede_device.h` function for function: fp64 of
  the reference = `Ff` pair (48 bits), fp64 libm call = pair function of
  `sycl_ff_math.h` (`sqrt`, `cbrt`, `pow_2_4`, `pow_7`, `exp`, `sin_cos`,
  `atan2`; 2^-44 or better), `float` of the reference = float, rounded
  from the pair at the reference's statement. `powf(x, 7)` / `powf(x, 2)`
  = correctly rounded (glibc's are not). Constants: `make_constants(bpc)`
  on the host from the reference's own expressions, by value into the
  kernel. Tables (`kAtanTable`, `kSinCosTable`): generated
  (`scripts/dev/gen_sycl_ff_math.py --write`), copied to device memory at
  the first submit, read through a pointer; NEVER index a constant array
  with a run-time value in a kernel (scratch, ADR-1395). `ciede_pixel()`
  carries `__attribute__((flatten, always_inline))` and the headers'
  functions `VMAF_SYCL_ALWAYS_INLINE`: without, some stay calls, the
  frames are scratch (3.4 KiB) and the A380 scores 27 dB off. Kernel
  stores one float per pixel at its raster position; host
  `ciede_frame_sum()` (`../ciede_frame_sum.h`, shared with the CUDA and HIP
  hosts) = `extract()`'s double sum, score
  `45. - 20. * log10(sum / (w * h))`. Never: device fp32 `pow` / `cbrt` /
  `atan2` / `sin` / `cos` / `exp` for a result, `7.787 t + 16 / 116`
  (that line alone was 1.12e-5), a device reduction. NOT exact: A380 vs
  GCC CPU 47 of 48 Netflix frames identical, BBB 4K within 1.4e-11 (18 of
  8.3 M pixels per frame, all glibc `powf`); gate `LIBM_TWINS` `sycl` =
  1e-9. Pair quotient / root = `vmaf_sycl_ffm::div()` / `sqrt()` on the
  device's own `/` and `sycl::sqrt` (second partial result comes from the
  exact residual; `ff_div()` + `sqrt_rn()` there = 82 ms, same values).
  The reference's FLOAT divisions stay `div_rn()`. Kernel shape pinned:
  `CiedeKernel`, SIMD-16, default register file (SIMD-32 = 38 ms but 16 KiB
  spills = scratch; grf 256 slower). Cost 50.3 ms per 4K frame (16.2
  before): `T-SYCL-CIEDE-EXACT-THROUGHPUT-2026-10-01`. Upstream change to
  `get_lab_color()` / `ciede2000()` / `get_r_sub_t()` / `extract()`'s sum
  -> this header + CUDA header same PR. Guards: `test_sycl_ciede_math`
  (pair functions vs extended-precision libm; pixel vs fp64 statements;
  host + device), `test_sycl_ciede_parity` (1e-8),
  `test_sycl_ciede_exact_contract.py` (10 planted regressions + generator
  check).

- **`picture_copy()` channel parameter** — `integer_ms_ssim_sycl.cpp`
  passes `channel=0` per d3647c73 prerequisite port
  (`integer_ssim_sycl.cpp` no longer calls `picture_copy()`, ADR-1370). See
  [../../AGENTS.md §"`picture_copy()` carries a `channel`
  parameter"](../../AGENTS.md).

- **`integer_cambi_sycl.cpp` — fully device-resident, graph-registered**
  ([ADR-1357](../../../../docs/adr/1357-sycl-cambi-device-resident.md),
  supersedes the ADR-0415 / ADR-0489 host residual). Reads distorted luma
  from shared frame (`enqueue_fn` `shared_dis`), no own upload. Whole frame
  = one `enqueue_cambi_work`: reset → validate → preprocess → tiled mask →
  per scale {decimate, filter H, filter V + level map Q, `launch_row_masks`,
  `launch_c_values` (+ radix pass 0 + per-group sum), `launch_topk_pooling`}.
  `post_fn` = only D2H (88-byte `CambiSyclResults`); `collect()` = only host
  arithmetic (`vmaf_cambi_weight_scores_per_scale`). Load-bearing:
  - every kernel argument init-time state: graph recording replays
    `enqueue_fn` for both slots; no host decision per frame, no
    `memset`/`fill` inside `enqueue_fn` (reset is a kernel);
  - c-values multiply by `vmaf_cambi_reciprocal_lut()` table, never
    `1.0f / i` (42 entries differ by 1 ulp);
  - top-K sum exact: 128-bit fixed point, units 2^-24 (all non-zero
    c-values in [0.5, 2^14)); no fp64, no float accumulation;
  - `check_window_fits_lut` = `cambi.c::setup_contrast_and_luminance`
    guard, same place (after TVI), same -EINVAL + message, both enc and
    source windows; LUT uploaded verbatim, never extended —
    `test_sycl_cambi_parity` window cases pin accept/reject set;
  - `CambiSyclSelect::k_rem[p + 1]` written by scan of pass p: no lane
    rewrites word another lane reads; bin 0 of pass 0 = exact zeros
    → `resolved`, later passes return on device;
  - histogram cells `uint16`, modular; order of updates free (true window
    counts), so run/skip rewrites keep bit-exactness.
  **On rebase**: `cambi.c` change to `c_value_pixel`,
  `calculate_c_values` window walk, `spatial_pooling`, preprocessing or
  `filter_mode` → mirror into device kernels same PR;
  `test_sycl_cambi_parity` asserts bit-exact per frame.

- **Per-step `q.wait()` in feature extractors forbidden — use
  in-order queue** (ADR-0458 / SY-1). SYCL in-order queue serialises
  all submitted operations automatically; adding `q.wait()` between GPU
  kernels drains queue to idle, prevents pipelining. Only
  mandatory `q.wait()` calls at **CPU-reads-from-device boundaries**
  (i.e., right before host code reads `vmaf_sycl_malloc_host` buffer
  written by preceding `q.memcpy`). Example: `integer_cambi_sycl.cpp`
  has none of its own — `collect()` reads its readback after
  `vmaf_sycl_graph_wait()` (ADR-1357).

- **Stencil/convolution SYCL kernels MUST use `local_accessor` for tap
  reuse** (ADR-0458 / SY-2). Separable filter (Gaussian, box,
  motion-blur) with more than 3 taps **must** stage required input
  region into shared local memory (SLM) via
  `local_accessor` + cooperative tile-load loop + barrier — follow
  pattern in `float_vif_sycl.cpp`, `float_motion_sycl.cpp`, and (post
  ADR-0458) `integer_ssim_sycl.cpp`.
  Bare `parallel_for<range<N>>` reading global memory for every tap =
  lint violation for convolution kernels — use `nd_range` instead.

- **`integer_adm_sycl.cpp` / `float_adm_sycl.cpp` expose three ADM
  tuning parameters** (`adm_csf_scale`, `adm_csf_diag_scale`,
  `noise_weight`) with same defaults as CPU path (PR #731).
  If upstream Netflix adds or renames these parameters in
  `integer_adm.c` / `float_adm.c`, SYCL twins must update
  in same PR.

- **`motion_fps_weight` cross-backend parity** — see canonical
  invariant note in [`../cuda/AGENTS.md`](../cuda/AGENTS.md).
  `integer_motion_v2_sycl.cpp` and `float_motion_sycl.cpp` both carry
  `motion_fps_weight` option, apply it in `flush()` /
  `collect()` exactly as documented there. Any future change to
  weight application math must span all motion-family GPU twins in
  same PR. Since ADR-1365 `float_motion_sycl.cpp` spells it the CPU
  way, `motion_clip(min(prev, cur))` = min, then weight, then
  `motion_max_val` cap; for weight >= 0 bit-identical to
  weight-before-min the note describes. v1 `integer_motion_sycl.cpp`
  twin covered by same canonical note's **applied exactly once**
  clause (ADR-1216): `motion3_postprocess_sycl()` must not re-apply
  weight its callers already applied.

- **`float_vif_sycl.cpp` options must be captured, not hardcoded**
  (ADR-1217) — see canonical note in
  [`../cuda/AGENTS.md`](../cuda/AGENTS.md). Compute kernel captures
  `vif_sigma_nsq`, `vif_enhn_gain_limit` and host-derived
  `sigma_max_inv` from `launch_compute`'s parameters; must not
  re-declare them as kernel-local constants.
- **SpEED singular-covariance contract** — see canonical note in
  [`../cuda/AGENTS.md`](../cuda/AGENTS.md). Since ADR-1358 the SYCL
  twins decide singularity on the device: `linalg_store()` in
  `speed_sycl_pipeline.cpp` writes the per-channel flag, and
  `block_statistics()` solves into a zero-initialised private solution
  that stays zero on a singular channel, so no device buffer is read
  before it is written. `score_group()` applies the one-sided rule and
  the flags reach the host in `FrameResult.singular`. ADR-1218.
- **`float_adm_sycl.cpp` options must be captured, not hardcoded**
  (ADR-1220) — see canonical note in
  [`../cuda/AGENTS.md`](../cuda/AGENTS.md). `launch_terms` captures
  `adm_p_norm` (`TermArgs.p_norm`, `.is_cube`) and `adm_bypass_cm`
  (`TermArgs.bypass_cm` -> thresholds 0); host pooling =
  `adm_pool_bands_s()` with `adm_noise_weight` + `adm_p_norm`.
- **`float_adm_sycl.cpp` = CPU `float_adm`, bit for bit
  ([ADR-1434](../../../../docs/adr/1434-sycl-float-adm-cpu-arithmetic.md),
  after ADR-1420 for CUDA).** Per-work-item code =
  `sycl_float_adm_math.h` (`decouple_sample()`, `terms_sample()`,
  `row_item()`), function for function with
  `../cuda/float_adm/float_adm_device.h`; the `.cpp` only launches.
  `divs()` = fp32 `n / d` = CPU `DIVS()` since ADR-1442 (reference
  divides on every host; no reciprocal, no probe, no table; device `/`
  correctly rounded under ADR-1367's flag line, checked per value by
  `test_decouple_csf_device`). Never again: a reciprocal in `divs()`;
  `cos^2 * (o^2 * t^2)` (reference:
  `(cos^2 * o^2) * t^2`; this alone was 1.28e-5 on BBB 4K); fp32 1/30,
  1/15 or gain (reference: `double`; no fp64 on device, ADR-0220 ->
  `times_constant()` / `add_scaled()` = exact fp32 pair, zone 2^-18 of a
  step, else integer replay on `SoftDouble`; `gain_limited()` = fp32
  product when the limit is an fp32 value, replay otherwise); centre tap
  anywhere but fifth; group reduction or a `double` fold (terms kernel
  stores nine terms per region sample, `row_item()` adds a row left to
  right at SG 8, host `fold_rows()` in fp32); own CSF weights, region,
  pooling root or floor (`adm_csf_rfactor_s()`, `adm_border_s()`,
  `adm_pool_bands_s()`, `1e-10 * (w * h) / (1920.0 * 1080.0)`).
  `adm_p_norm`: 3 = `(x * x) * x`, 1 = `x`, else device `pow` (1.8e-7
  from CPU, not exact). Options = CPU's incl. `adm_f1sN` / `adm_f2sN`,
  `adm_skip_aim_scale`, `adm_skip_scale0`; `adm_csf_mode` != 0 still
  `-EINVAL`. Upstream change to `adm_decouple_s()`, `adm_csf_s()`,
  `adm_cm_thresh3x3_s()`, `adm_csf_den_scale_s()`, `adm_cm_s()` ->
  header + CUDA header same PR. Exact vs the CPU extractor of the SAME
  build: host `powf` in `adm_pool_bands_s()` is libimf under icx, glibc
  under GCC (`aim` 1.6e-9 on 2 of 200 BBB frames between the two CPU
  builds, `T-ICX-LIBIMF-HOST-MATH-2026-10-01`). 4K frame 15.1 -> 12.3 ms.
  Scratch-free (ADR-1395).
  Exact twin: `scripts/ci/exact_twins.d/float_adm.sycl`. Guards:
  `test_sycl_float_adm_math` (host + device vs `adm_tools.c`),
  `test_sycl_float_adm_parity` (`==`, cases in
  `core/test/float_adm_twin_parity.h`),
  `test_sycl_float_adm_exact_contract.py` (15 planted regressions).

- **VAAPI / dmabuf zero-copy import** — FFmpeg `libvmaf_sycl`
  filter (`ffmpeg-patches/0005-*.patch`) consumes
  `vmaf_sycl_import_va_surface`. Public-surface change touches
  patch file too — see CLAUDE.md §12 r14 +
  [ADR-0183](../../../../docs/adr/0183-ffmpeg-libvmaf-sycl-filter.md).

- **`ssimulacra2_sycl.cpp` IIR recurrence has no running accumulator —**
  **never 'Kahan' it** (ADR-0985). Charalampidis recursive blur = 3-pole
  autoregressive IIR filter
  ($o_k = n2 \cdot \text{sum} - d1 \cdot \text{prev1} - \text{prev2}$), not
  cumulative summation. Adding accumulator term $\text{prev1}$ into
  output shifts poles outside unit circle ($1 - d1 \approx -0.8422$),
  causing geometric pole blow-up to $10^{25}$ / NaN / saturation at 100.0.
  Recurrence must remain pure float32 matching CUDA twin
  `core/src/feature/cuda/ssimulacra2/ssimulacra2_blur.cu`. The places=1
  (`5.0e-2`) Arc A380 calibration of that time is gone: twin exact since
  ADR-1446.

- **`ssimulacra2_sycl.cpp` is device-resident
  ([ADR-1363](../../../../docs/adr/1363-sycl-ssimulacra2-msssim-device-resident.md)).**
  `submit()` uploads the six raw planes and enqueues the whole frame (YUV ->
  linear RGB, per scale XYB, the three products and five blurs, SSIM / edge
  sums, downsample) plus one copy of the per-scale
  sums; `collect()` is the only wait. Load-bearing: the TU builds with
  contraction off (every feature TU does, ADR-1367); `VMAF_SS2_FDIV` maps the shared
  `vmaf_ss2_cbrtf` division to `div_rn` before `ssimulacra2_math.h` is
  included; products feeding adds sit in named temporaries; YUV / XYB /
  blur / downsample are bit-identical to `ssimulacra2.c` and the parity sweep
  proves it. The sums of the per-pixel fp64 terms of `ssim_map` /
  `edge_diff_map` are the CPU's too (ADR-1446, next bullet).
  `check_context_sycl` routes the inputs init rejects
  (4:0:0, a side below 8) to the CPU `ssimulacra2` (ADR-1324 / ADR-1359); keep
  it in step with `init_fex_sycl`. **On rebase**: do not reintroduce a host
  stage or a mid-frame wait (`test_sycl_kernel_source_contract.py` fails),
  and keep the
  per-channel sum order (L1, L4, artifact, artifact^4, detail, detail^4) that
  `ss2s_scale_norms` reads. Two blur variants measured slower (ADR-1363): the
  products fused into the horizontal pass (2.3x on the UHD 770, which is bound
  by the per-lane loads of the row walk) and the rows staged through local
  memory (2x on the B580, 5.6x on the UHD 770); do not retry either without
  measuring per stage.

- **Twins declared exact as a group
  ([ADR-1451](../../../../docs/adr/1451-sycl-exact-twins-declared.md),
  `scripts/ci/exact_twins.d/`).** `adm`, `motion`, `motion_debug`,
  `motion_v2`, `psnr`, `float_ssim`, `float_ssim_lcs`, `cambi`: `sycl`
  listed -> gate tolerance 0. Basis per twin: integer sums on device + CPU's
  host arithmetic (`adm_sycl` ADR-1362, `motion_sycl` / `motion_v2_sycl`
  ADR-1371, `psnr_sycl` ADR-1365), CPU's window arithmetic type for type +
  integer frame sums (`float_ssim_sycl`, ADR-1370 / ADR-1414), integer
  pipeline + exact top-K sum (`cambi_sycl`, ADR-1357). Rule: listed = by
  construction AND measured identical on full-range noise at 8 / 10 / 12 /
  16 bit, never on measurement alone. A listed twin that drifts is FIXED,
  never given a tolerance. `test_sycl_exact_twins` = `==` on every output,
  8 + 10 bit. NOT listed: `speed_chroma` (device `log2` correctly rounded,
  CPU = build's libm; identical on 333 frames, library-dependent), `ciede`
  (libm bound). With the 11 twins exact by their own ADRs: 19 of 21 gate
  features. Default-model VMAF on A380 = CPU's on every frame measured.

- **`float_psnr_sycl` = CPU `float_psnr`, bit for bit
  ([ADR-1450](../../../../docs/adr/1450-sycl-float-psnr-exact-block-sums.md)).**
  CPU: `diff * diff` in `float`, terms added in double (exact below 2^53
  units of 1 / scaler^2). Kernel: `fpsnr_pixel_noise()` = fp32 square of
  the RAW integer difference, as `uint64` (same significand as CPU's term:
  power-of-two scaling); sub-group reduce, work-group total, read-back and
  host sum all `uint64`; host: `((double)total / scaler^2) / n_pix`. NEVER
  an fp32 or fp64 group sum (fp32 exact only to 24 bits: 2.4e-8 dB off on
  12-bit noise, 7.4e-8 on bright 16-bit), never an integer square (CPU
  rounds the square to 24 bits at 16 bit). Both helpers
  `VMAF_SYCL_ALWAYS_INLINE` (ADR-1395). 16 bit: CPU's own sum rounds past
  MSE x pixels = 2^37 (8-bit scale); twin then within 7e-13 dB. High-bit
  Netflix fixtures = 8-bit shifted left, show nothing: use full-range noise.
  Host `log10` = build's libm (`T-ICX-LIBIMF-HOST-MATH-2026-10-01`).
  Guards: `test_sycl_float_psnr_parity` (+ `_large`),
  `test_sycl_float_psnr_exact_contract.py`.

- **`float_moment_sycl` = CPU `float_moment`, bit for bit
  ([ADR-1449](../../../../docs/adr/1449-sycl-float-moment-cpu-float-squares.md)).**
  CPU: `moment.c` squares each sample in `float`, adds floats into ONE
  double per output. Kernel (`integer_moment_sycl.cpp`): four `int64`
  sums; second sums add `moment_float_square()` = one fp32 product of the
  raw sample, as integer below 2^32 (= CPU's term in units of 1 / scaler^2;
  = integer square up to 12 bit, rounded to 24 bits at 16). Host: CPU's two
  divisions. NEVER `r * r` in integers (1.0e-4 off at 16 bit), never an
  fp64 square. Exact while sum < 2^53 units (every frame <= 2^21 pixels,
  every 8/10/12-bit frame); past it CPU's sum rounds per add, twin within
  derived bound (`T-HIP-FLOAT-MOMENT-PAST-2-53-2026-10-02`). Helper is
  `VMAF_SYCL_ALWAYS_INLINE` (call in kernel = scratch frame, ADR-1395).
  16-bit Netflix fixture = 8-bit shifted left, shows nothing: use full-range
  content. Guards: `test_sycl_float_moment_parity` (+ `_large`),
  `test_sycl_float_moment_exact_contract.py`. Reduction = four atomics per
  pixel, 28.7 ms per 4K frame on A380:
  `T-SYCL-FLOAT-MOMENT-PER-PIXEL-ATOMICS-2026-10-02`.

- **`ssimulacra2_sycl` = CPU `ssimulacra2`, bit for bit
  ([ADR-1446](../../../../docs/adr/1446-sycl-ssimulacra2-cpu-bits.md)).**
  CPU: six fp64 terms per sample and channel, each added pixel after pixel
  into ONE double. Twin, no fp64 type:
  - Terms = `sycl_ssimulacra2_math.h` (`ssim_terms()`, `edge_terms()`): the
    reference's fp64 operations one for one on `SoftSigned`
    (`sycl_soft_signed.h`), from the fp32 values the reference converts
    (`ss2s_ssim_inputs()` = fp32 part verbatim). Returns fp64 bit patterns.
    Zero denominator: quotient = infinity of the product's sign, clamped
    `d` = 0. Value the reference makes infinite or NaN -> NaN bits (sum keeps
    it, frame guard rejects, as CPU). Zero has no sign.
  - Sums = `sycl_ordered_sum.h` on `../ordered_sum.h` with
    `VMAF_ORDSUM_NO_FP64` (`_bits` forms only). Chunk = `SS2S_CHUNK` (512)
    consecutive pixels = one work-group, 256 lanes x 2 CONSECUTIVE pixels.
    Per scale: `launch_chunk_sums` (fp32 pair terms, fp32 tree: ADVICE only;
    fourth powers scaled by 2^88) -> `launch_chunk_plan`
    (`plan_chunks()`, one work-item per sum) -> `Ss2SsimUnitsKernel` /
    `Ss2EdgeUnitsKernel` (exact terms -> integer increments under the plan,
    composed in pixel order by `ss2s_ordered_tree`, lane `i` takes lane
    `i + stride`) -> `Ss2SlotKernel` (chunks planned "term by term": keep
    the terms, compose runs of 16 under the two binades the chunk ends in)
    -> `Ss2TotalsKernel` (`walk_sum()`, ONE lane per sum).
  - Rules: (1) advice sums never reach a result: plan only; (2) plan =
    advice: the walk adds a chunk or a run from its increment only when
    `vmaf_ordsum_add_chunk_bits()` accepts it at the exact sum, never drop
    that check; (3) composition (`vmaf_ordsum_then`) NOT commutative: earlier
    pixels on the left, no halving tree, no strided lanes; (4) terms >= 0 or
    NaN only; (5) change to the terms in `ssimulacra2.c` ->
    `sycl_ssimulacra2_math.h` + `reference_terms()` of its test, same PR;
    (6) `ordered_sum.h` is shared with CUDA and HIP: fp64 forms stay
    wrappers of the `_bits` forms, no `double` outside
    `#ifndef VMAF_ORDSUM_NO_FP64`.
  - Why slots and runs: one A380 lane takes about 1 us per fp64 add in
    integers; a crossing chunk walked term by term = 512 such steps. Runs
    make it about 32 integer steps + 16 terms. Wrong advice, no slot, wrong
    expected binade = slower, same bits.
  - Shapes (scratch-free on A380, ADR-1395): units SIMD-16 default register
    file, slot kernel SIMD-16 + 256-entry file, walk SIMD-8. SIMD-32 spills;
    chunk 1024 spills in the slot kernel without the large file (wrong
    values, runs that hang past 300 s).
  - Guards: `test_sycl_ssimulacra2_math` (terms vs reference lines, host +
    device), `test_sycl_ordered_sum` (sum vs loop, wrong plans, host +
    device walk), `test_sycl_ssimulacra2_parity` + `_large` (`==`; 15 of 16
    score cases differ on the old twin),
    `test_sycl_ssimulacra2_exact_contract.py`, `test_sycl_kernel_scratch`.
    Cost + tuning candidates:
    `T-SYCL-SSIMULACRA2-EXACT-THROUGHPUT-2026-10-02`.

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

## icpx-aware clang-tidy

Stock LLVM `clang-tidy` cannot resolve `<sycl/sycl.hpp>`. Use
[`scripts/ci/clang-tidy-sycl.sh`](../../../../scripts/ci/clang-tidy-sycl.sh),
which injects oneAPI SYCL include path +
`-D__SYCL_DEVICE_ONLY__=0`, locates `icpx` via `$ICPX_ROOT` (or
`/opt/intel/oneapi/compiler/latest`). CI lane
`Tidy SYCL` runs wrapper. Required check since ADR-1297;
no longer advisory, no `continue-on-error`.
Adding new SYCL TU needs no AGENTS.md update — wrapper
finds it via changed-file diff. See
[ADR-0217](../../../../docs/adr/0217-sycl-toolchain-cleanup.md).

## Build

SYCL feature TUs compile only when `meson setup -Denable_sycl=true`.
Requires oneAPI (`source /opt/intel/oneapi/setvars.sh`) or equivalent
DPC++ toolchain with `icpx` on PATH.

## Governing ADRs

- [ADR-0182](../../../../docs/adr/0182-gpu-long-tail-batch-1.md) +
  [ADR-0188](../../../../docs/adr/0188-gpu-long-tail-batch-2.md) +
  [ADR-0192](../../../../docs/adr/0192-gpu-long-tail-batch-3.md) —
  GPU long-tail batches. Every SYCL feature kernel here = row
  in one of these.
- [ADR-0202](../../../../docs/adr/0202-float-adm-cuda-sycl.md) +
  [ADR-0206](../../../../docs/adr/0206-ssimulacra2-cuda-sycl.md) —
  CUDA + SYCL ports pinning `-fp-model=precise` as load-bearing.
- [ADR-0214](../../../../docs/adr/0214-gpu-parity-ci-gate.md) —
  GPU-parity CI gate.
- [ADR-0217](../../../../docs/adr/0217-sycl-toolchain-cleanup.md) —
  icpx-aware clang-tidy wrapper.
- [ADR-0219](../../../../docs/adr/0219-motion3-gpu-contract.md) —
  motion3 GPU contract.
- [ADR-0220](../../../../docs/adr/0220-sycl-fp64-fallback.md) — SYCL
  feature kernels unconditionally fp64-free (T7-17).
- [ADR-0243](../../../../docs/adr/0243-enable-lcs-gpu.md) — MS-SSIM
  `enable_lcs` GPU contract.
- [ADR-0985](../../../../docs/adr/0985-sycl-parity-divergence-2026-06-03.md) —
  SYCL SSIMULACRA 2 parity divergence and recurrence resolution.

## Per-kernel parity-test invariant (rounds 1–3)

Every SYCL feature kernel here has a scalar reference and
`core/test/test_sycl_<kernel>_parity.c` gate. Most use ADR-0214 places=4
(1e-4) tolerance; `motion_add_uv` uses ADR-1326's exact fixed-point oracle
because its CPU float semantic twin has different arithmetic. Coverage matrix
below tracks which SYCL kernel maps to which CPU twin and which parity test.
**On rebase**: if SYCL kernel renamed or new one added, parity test name +
ADR-0884 / ADR-0946 backlog must update in same PR.

| SYCL TU | CPU TU | Parity test | ADR |
|---|---|---|---|
| `integer_cambi_sycl.cpp` | `cambi.c` | `test_sycl_cambi_parity.c` (bit-exact, 4 frames), `test_integer_cambi_sycl.c` (smoke) | [ADR-1357](../../../../docs/adr/1357-sycl-cambi-device-resident.md) |
| `integer_motion_sycl.cpp` (motion3) | `integer_motion.c` | `test_sycl_motion3_parity.c` | ADR-0219 |
| `integer_motion_pipeline_sycl.cpp` (motion + motion_v2 SAD) | `integer_motion.c`, `integer_motion_v2.c` | `test_sycl_motion_tiny_frames.c` (bit-exact, 3x3 .. 1283x723) | T-SYCL-MOTION-TINY-FRAME-PARITY-2026-09-29 |
| `integer_motion_sycl.cpp` (motion_add_uv) | `float_motion.c` | `test_sycl_motion_add_uv_parity.c` | ADR-0989 |
| `integer_psnr_sycl.cpp` | `integer_psnr.c` | `test_sycl_psnr_parity.c` | ADR-0868 (round 1) |
| `integer_vif_sycl.cpp` | `integer_vif.c` | `test_sycl_vif_parity.c` (bit-exact, every output, 8 / 10 bit), `test_sycl_integer_vif_math.c` | ADR-0868 (round 1), ADR-1432 |
| `integer_adm_sycl.cpp` | `integer_adm.c` | `test_sycl_adm_parity.c` | ADR-0884 (round 2) |
| `integer_ciede_sycl.cpp` | `ciede.c` | `test_sycl_ciede_parity.c` (1e-8, 8 to 16 bit, 4:2:0 / 4:2:2 / 4:4:4), `test_sycl_ciede_math.c` | ADR-0884 (round 2), ADR-1436 |
| `integer_ssim_sycl.cpp` | `integer_ssim.c` | `test_sycl_ssim_parity.c` (+ `_large`; bit-exact, 8 to 16 bit), `test_sycl_integer_ssim_math.c` | ADR-0884 (round 2), ADR-1443 |
| `integer_ssim_sycl.cpp` (`float_ssim_sycl`) | `float_ssim.c` + `ssim.c` | `test_sycl_float_ssim_parity.c` (+ `_large`) | ADR-1370 |
| `integer_ms_ssim_sycl.cpp` | `ms_ssim.c` | `test_sycl_ms_ssim_parity.c` (+ `_large`; bit-exact, 18 outputs x 3 frames) | ADR-0884 (round 2), ADR-1414 |
| `integer_motion_v2_sycl.cpp` | `integer_motion_v2.c` | `test_sycl_motion_v2_parity.c` | ADR-0884 (round 2) |
| `float_psnr_sycl.cpp` | `float_psnr.c` | `test_sycl_float_psnr_parity.c` (+ `_large`; bit-exact, 8 to 16 bit, `uncapped`; bound past 2^53) | ADR-0946 (round 3), ADR-1450 |
| `float_adm_sycl.cpp` | `float_adm.c` | `test_sycl_float_adm_parity.c` (bit-exact, every output, 8 to 16 bit), `test_sycl_float_adm_math.c` | ADR-0946 (round 3), ADR-1434 |
| `float_vif_sycl.cpp` | `float_vif.c` | `test_sycl_float_vif_parity.c` (bit-exact, every output, 8 / 10 bit), `test_sycl_float_vif_math.c` | ADR-0946 (round 3), ADR-1422 |
| `float_motion_sycl.cpp` | `float_motion.c` | `test_sycl_float_motion_parity.c` (bit-exact, every frame, 8 / 10 / 12 bit) | ADR-0946 (round 3), ADR-1411 |
| `integer_psnr_hvs_sycl.cpp` | `third_party/xiph/psnr_hvs.c` | `test_sycl_psnr_hvs_parity.c` | ADR-0946 (round 3) |
| `integer_moment_sycl.cpp` (`float_moment_sycl`) | `float_moment.c` | `test_sycl_float_moment_parity.c` (+ `_large`; bit-exact, 8 to 16 bit; bound past 2^53) | ADR-0957 (round 4), ADR-1449 |
| `speed_chroma_sycl.cpp` + `speed_sycl_pipeline.cpp` | `speed.c` | `test_sycl_speed_chroma_parity.c`, `test_sycl_speed_singular_parity.c` | ADR-0957 (round 4), ADR-1358 |
| `speed_temporal_sycl.cpp` + `speed_sycl_pipeline.cpp` | `speed.c` | `test_sycl_speed_temporal_parity.c`, `test_sycl_speed_singular_parity.c` | ADR-0957 (round 4), ADR-1358 |
| `ssimulacra2_sycl.cpp` | `ssimulacra2.c` | `test_sycl_ssimulacra2_parity.c` (+ `_large`; bit-exact, 8 to 16 bit, 4:2:0 / 4:2:2 / 4:4:4), `test_sycl_ssimulacra2_math.c`, `test_sycl_ordered_sum.c` | ADR-0957 (round 4), ADR-1363, ADR-1446 |

> **SpEED twins are wired and device-resident (ADR-0964, ADR-1358).**
> Both extractors are in `sycl_feature_sources` with the shared
> `speed_sycl_pipeline.cpp` and `speed_sycl_host.cpp`. Their parity tests
> are live gates; on real video the twins match the CPU bit for bit (see
> `docs/metrics/speed_qa.md`).

## Per-feature option-table sync invariant

**Adding feature knob to any one backend (SYCL / CUDA / HIP / Metal /
Vulkan) requires adding it to all backends in same PR** — no deferred
follow-ups. Canonical source of truth for option signature (name,
alias, type, min, max, default, flags) = CPU feature extractor in
`core/src/feature/` (e.g. `integer_motion.c`). GPU twins copy
option entry verbatim, apply weight in equivalent host-side
`flush()` or post-processing callback.

Rationale: CHUG / K150K extractor whitelist in
`ai/scripts/extract_k150k_features.py` passes `_feature_arg` dicts to
`vmaf_use_features_with_opts`; if receiving backend's options table
misses knob, option silently falls through to default,
producing silently-wrong scores without any error. Root cause
of `motion_fps_weight` gap in `integer_motion_v2_sycl.cpp`, closed by
PR #851-follow-up (2026-05-16).

Second instance (ADR-1179, `fix/sycl-v1-model-crash`): `options_cambi_sycl`
lacked `cambi_high_res_speedup` (`hrs`). That knob carries
`VMAF_OPT_FLAG_FEATURE_PARAM`; its absence changed *serialised feature
name* — SYCL twin emitted `cambi_cmxv_17_vlt_0.06` while default
model `vmaf_v1.0.16_3d0h` asks for `cambi_hrs_1080_cmxv_17_vlt_0.06` —
prediction failed with `-EAGAIN` instead of falling through to default.
Two rebase-sensitive consequences: (1) every `VMAF_OPT_FLAG_FEATURE_PARAM`
knob of `cambi.c` must exist verbatim in `options_cambi_sycl`; (2)
`vmaf_feature_name_dict_from_provided_features()` must run in
`init_fex_sycl` **before** `enc_width` / `enc_height` / `enc_bitdepth`
default from picture geometry (same ordering as `cambi.c`),
else geometry defaults leak into feature name. TVI / VLT
tables come from shared `vmaf_cambi_init_tvi_and_vlt()` in `cambi.c`
— do not reintroduce private bisection in twin.

## Per-kernel parity-test invariant (ADR-0214 + ADR-0868 + ADR-0884)

**Every shipping SYCL kernel here must have a scalar-vs-SYCL parity test
under [`core/test/`](../../../test/), wired into
[`core/test/meson.build`](../../../test/meson.build) with suite
`['fast', 'gpu']`.** A parity test normally asserts the headline score
matches its CPU scalar reference within ADR-0214 places=4 (`1e-4`);
`motion_add_uv` instead matches an arithmetic-identical fixed-point oracle
within ADR-1326's derived host-double bound. Tests skip cleanly when no SYCL
device visible — mirrors `[skip: no SYCL device]` pattern in
[`test_sycl_motion3_parity.c`](../../../test/test_sycl_motion3_parity.c).

Coverage matrix:

| Kernel TU | Parity test | ADR |
|---|---|---|
| `integer_psnr_sycl.cpp` | `test_sycl_psnr_parity.c` | [ADR-0868](../../../../docs/adr/0868-gpu-backend-kernel-coverage.md) |
| `integer_vif_sycl.cpp` | `test_sycl_vif_parity.c` | ADR-0868 |
| `integer_adm_sycl.cpp` | `test_sycl_adm_parity.c` | [ADR-0884](../../../../docs/adr/0884-sycl-kernel-coverage-round2.md) |
| `integer_ciede_sycl.cpp` | `test_sycl_ciede_parity.c` | ADR-0884 |
| `integer_ssim_sycl.cpp` (integer fex) | `test_sycl_ssim_parity.c` | ADR-0884 |
| `integer_ms_ssim_sycl.cpp` | `test_sycl_ms_ssim_parity.c` | ADR-0884 |
| `integer_motion_v2_sycl.cpp` | `test_sycl_motion_v2_parity.c` | ADR-0884 |
| `integer_motion_sycl.cpp` | `test_sycl_motion3_parity.c` | [ADR-0219](../../../../docs/adr/0219-motion3-gpu-contract.md) |
| `integer_motion_sycl.cpp` (motion_add_uv) | `test_sycl_motion_add_uv_parity.c` | [ADR-0989](../../../../docs/adr/0989-sycl-motion-add-uv.md) |
| `integer_cambi_sycl.cpp` | `test_sycl_cambi_parity.c` (bit-exact per frame) + `test_integer_cambi_sycl.c` (smoke) | [ADR-0415](../../../../docs/adr/0415-cambi-sycl-port.md), [ADR-1357](../../../../docs/adr/1357-sycl-cambi-device-resident.md) |
| `float_*_sycl.cpp`, `speed_*_sycl.cpp`, `ssimulacra2_sycl.cpp`, `integer_moment_sycl.cpp`, `integer_psnr_hvs_sycl.cpp` | (round 3 backlog — see ADR-0884) | — |

**Rebase-sensitive**: adding new SYCL kernel TU, same PR
must add matching `test_sycl_<kernel>_parity.c` and meson
wiring. `/cross-backend-diff` skill = dev-time tool only,
does NOT run in CI on every PR; only in-tree repository-runner parity
tests catch per-kernel regressions automatically.

## motion3_v2 cross-twin invariant (ADR-1108)

- `integer_motion_v2_sycl` emits `motion3_v2_score` host-side in
  flush, mirroring CPU `integer_motion_v2.c::flush` and CUDA twin
  byte-for-byte: per-frame `motion_blend(motion2, blend_factor,
  blend_offset)` then `MIN(_, motion_max_val)` clip, a `stamp_value` seed
  for `i < min_idx (= 1)`, and optional 2-tap `motion_moving_average`,
  via shared `motion_blend_tools.h` helper. Any change to CPU
  flush blend/clip/seed/average logic must mirror into all four GPU
  twins (cuda/sycl/hip/metal) in same PR to keep `places=4`
  `test_sycl_motion_v2_parity` gate green.

## Integer ADM tiny frames and linkage (T-GPU-ADM-TINY-FRAME-SHIFT-2026-09-18)

- `init_fex_sycl()` calls `adm_frame_size_check()` first, before any device
  resource. Bound = CPU bound (17x17).
- `integer_adm_sycl.cpp` internals live in one anonymous namespace; only the
  `extern "C"` extractor struct has external linkage. No C-style `static` at
  file scope, no linkage NOLINT band.
- Scale 0 = CPU int16 semantics (T-SYCL-ADM-INT16-SEMANTICS-2026-09-18). CPU
  stores bands as `int16_t`; kernels compute wider, so wrap explicitly with
  `adm_i16()` (mod 2^16, compiler-independent) wherever the CPU narrows:
  `csf_a`, `csf_f`. Never widen these. Diagonal `csf_a` rounds with 65535,
  not `1 << 16`. NOT narrowed since ADR-1402: CM 1/15 centre tap
  (`adm_dev_csf_centre()`, int32 like CPU `adm_cm_thresh()`); scale-0 CM
  excess = `adm_dev_cm_excess_s0()`, int64, capped at INT32_MAX like CPU
  `adm_cm_excess_s0()`. Never put `adm_i16()` back on the centre term.
- Scales 1-3 `>> 32` rounding term = `I4_FLT_ROUND` = -2^31: the CPU's
  wrapped `(int32_t)(1u << 31)` (Netflix#955, ADR-0155), same as CUDA / HIP.
  Netflix fixes #955 -> change CPU and this constant together.
- Host finalisation = the CPU's float arithmetic (ADR-1362) -> every ADM
  output bit-exact. The old double finaliser (`conclude_adm_cm` /
  `conclude_adm_csf_den`, ~1e-7 residual) is gone; do not bring it back.
- Guard: `test_sycl_adm_tiny_frames` (tiny frames, full-range noise,
  isolated patches; bit-exact arm).

## Integer ADM AIM pass (ADR-1362, T-GPU-ADM-AIM-DEVICE-PASS-MISSING-SYCL-HIP-2026-09-05)

- `adm_sycl` provides `VMAF_integer_feature_aim_score` +
  `VMAF_integer_feature_adm3_score` -> default model `vmaf_v1.0.16_3d0h` runs
  its whole ADM branch on the device. AIM = CPU `measure_aim`: threshold from
  csf(r) (3x3 `|csf(r)| / 30` + 1/15 centre), measure t - r, no noise floor.
- Per scale 2 kernels after the DWT: `launch_decouple_csf` stores
  `d_csf_f` (`|csf(t - r)| / 30`) + `d_csf_f_aim` (`|csf(r)| / 30`) over the
  whole band; `launch_csf_den_cm` = one work-group per region row, all 3
  bands, 9 sums (CM, CSF den, AIM). r and t - r recomputed per sample
  (`adm_dev_sample`); storing r measured slower on the UHD 770 (bandwidth).
- One accumulator buffer `[term][scale][band]` (`adm_accum_slot`, 36 int64):
  one memset in `pre_fn`, one D2H in `post_fn`, read in `collect()` after
  `vmaf_sycl_graph_wait()`. No host wait inside a frame; keep it that way.
- Row fold through `adm_cm_round_row_total()` in `adm_dev_fold_row`, once per
  row per sum (ADR-1167). `test_adm_cm_row_rounding_contract.py` pins it.
- `adm_dev_decouple_k` clamps the Q15 quotient in int64 like the CPU's
  `tmp_k`. Old kernel narrowed to int32 first -> |t / o| > 2^16 at scales 1-3
  wrapped -> `integer_adm_scale2` up to 1.40e-6 off the CPU at 4K, aim not
  exact. Never
  narrow before the clamp.
- All outputs (adm2, scale*, debug num / den, aim, adm3) finalised in the
  CPU's own float arithmetic (`adm_cm_scale_cpu`, `adm_den_scale_cpu`,
  `adm_scale_cpu`, `adm_terms`, `adm_finalise`: float per band, float per
  scale, double sum, `(float)1e-10` skip-scale0 den, floor in place) ->
  bit-exact. Maintainer contract: bit-exact with the CPU. Any double
  shortcut here breaks `test_sycl_adm_parity` / `test_sycl_adm_tiny_frames`.
- CM kernel sub-group size 16: 9 int64 sums spill at 32 lanes on Xe-LP.
- `adm_skip_aim` mirrors the CPU: AIM sums skipped, aim = 0.
- Non-integer `adm_enhn_gain_limit` (e.g. 1.2): bit-exact too (ADR-1413).
  `adm_dev_gain_limit()` -> `adm_gain_limit_product()` = `trunc(fl(r * gain))`
  from integers. Do not bring back Q31 (`gain_limit_to_q31`): floor + limit
  rounded to 2^-31 -> `5 * 1.2` = 5, CPU stores 6; scale0 up to 3.6e-5 off.
  Guard: `test_gpu_adm_fractional_gain_limit_parity`, `test_adm_gain_limit`.
- Guards: `test_sycl_adm_parity` (`test_adm_cpu_sycl_aim_bit_exact`),
  `test_sycl_adm_tiny_frames` (aim / adm3 bit-exact under `HAVE_SYCL`: tiny,
  noise, 16-bit, CSF modes 0-3), `python/test/gpu_default_model_test.py`.
