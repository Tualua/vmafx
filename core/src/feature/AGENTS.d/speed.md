---
paths:
  - core/src/feature/speed.c
  - core/src/feature/speed_cov.h
invariant: SpEED buffer allocation, chroma dimensions, anti-alias decimation, and covariance sums.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# SpEED Buffers, Chroma Geometry, and Decimation

- **SPEED and CAMBI feature decomposition and NULL preservation** (fork-local,
  ADR-1146): [`cambi.c`](../cambi.c), [`cambi.h`](../cambi.h), [`speed.c`](../speed.c),
  [`speed_qa.c`](../speed_qa.c), and SIMD twins in [`x86/`](../x86/)
  (`cambi_avx2.c`, `cambi_avx512.c`, `speed_avx2.c`, `speed_avx512.c`) are
  decomposed into static single-purpose helpers to satisfy
  `readability-function-size` (≤60 lines, max nesting 4). C TUs preserve `NULL`
  through file-scoped `/* NOLINTBEGIN(modernize-use-nullptr) */` /
  `/* NOLINTEND(modernize-use-nullptr) */` brackets (ADR-1138). **On rebase**:
  when merging upstream changes to `calculate_c_values`, `init`, or
  `est_params`, map changes into respective decomposed helpers
  (`c_values_*`, `validate_and_setup_dimensions`, `alloc_cambi_buffers`,
  `solve_covariance_system`) rather than re-inlining. Shared prototypes in
  `cambi_internal.h` and `speed_internal.h` must not change without mirroring
  to all GPU/CPU twins (verified by `scripts/ci/twin-drift-check.sh`). Numerical
  bit-exactness is governed by ADR-1146.

## `speed_chroma` / `speed_temporal` are float-build-only

two upstream Speed extractors register inside
`#if VMAF_FLOAT_FEATURES` block in `feature_extractor.c`. They
are absent from default `meson setup` build; users who want
them must pass `-Denable_float=true`. Do **not** lift them out
of `#if` block — they call into Speed-specific helpers
in `vif_tools.c` that are themselves only compiled in
float-features path.

[ADR-0253](../../../../docs/adr/0253-speed-qa-extractor.md)
(Proposed) records deferral on extending this surface with
SpEED-QA full-frame reduction or SpEED-driven model. Status quo
is binding contract until one of three named triggers in
that ADR fires.

## `speed_temporal` frame buffers hold `alloc_height` rows (Netflix/vmaf#1626)

`speed_temporal` `init()` in [`speed.c`](../speed.c) sizes its four frame
buffers `float_stride * dimensions.alloc_height`. `filter_and_downscale()`
copies `alloc_height` rows out of each buffer and resamples the frame back in
place at `scaled_height`; `speed_prescale` above 1 makes that taller than the
source. Upstream still allocates `float_stride * h` (issue #1626, fix proposed
upstream in PR #1627). **On upstream sync**: keep `alloc_height`;
restoring `h` brings the heap overrun back.
`test_speed_frame_buffers` (1.0 / 1.5 / 2.0 / 4.0) catches it under ASan only.
Drop this note once the upstream fix is ported. `speed_chroma` and the
CUDA / HIP / SYCL twins already size from the scaled geometry.

## `speed_chroma` buffers use `speed_chroma_dimensions()` ceiling extents

In subsampled formats (4:2:0, 4:2:2) an odd luma width or height produces
an extra chroma row or column to cover the last luma sample
(`vmaf_chroma_extent()`). `speed.c` `init_chroma()`, `speed_chroma_cuda.c`,
and `speed_chroma_hip.c` must derive chroma extents via
`speed_chroma_dimensions()`, not integer `/ 2`, so that buffers match the
ceiling dimensions allocated by `picture.c` and copied by `picture_copy()`.

## SpEED anti-alias + decimation: fused off x86 (Netflix/vmaf 76ea5f03)

`filter_and_downscale()` in [`speed.c`](../speed.c) + mirror
`speed_internal_filter_and_downscale()` in [`speed_internal.c`](../speed_internal.c):

- `#if ARCH_X86`: `vif_filter1d_s()` (AVX2 convolution when dispatched) +
  `vif_dec16_s()`, unchanged. x86 output: no bit moves.
- every other target: `vif_filter1d_dec16_s()` ([`vif_tools.c`](../vif_tools.c))
  into scratch plane, then row-wise copy of `downscaled_w` floats back into
  frame buffer. Vertical pass at every 16th row via
  `vif_filter1d_vertical_s()` (shared with `vif_filter1d_s()`), horizontal
  pass at every 16th column via `vif_filter1d_horizontal_dec16_s()`.

Invariant: `vif_filter1d_dec16_s()` output == scalar `vif_filter1d_s()` +
`vif_dec16_s()` bit for bit (same taps, mirror, accumulation order).
`test_speed_filter` compares with `memcmp` (1620 size / layout / pattern /
filter-width cases, plus `speed_internal_filter_and_downscale()` whole-frame).
After any edit to scalar passes in `vif_tools.c`: run `test_speed_filter` on
aarch64 cross build under qemu. Change `vif_filter1d_horizontal_s()` or mirror
-> change `vif_filter1d_horizontal_dec16_s()` in same PR. Change one of
`speed.c` / `speed_internal.c` here -> change other one too.

GPU twins (`cuda/speed/speed_score.cu`, `hip/speed/speed_hip_device.h`,
`sycl/speed_sycl_pipeline.cpp`) already evaluate filter at decimated samples
only, in scalar arithmetic; port needed comment edits only.

**On upstream sync**: upstream branch `speed-fused-avx2` moves x86 to fused
AVX2 path. Port only with x86 before/after JSON identity at `--precision max`
(scalar, AVX2, AVX-512 dispatch).

## SpEED covariance sums: row kernels return scalar's bits (ADR-1459)

[`speed_cov.h`](../speed_cov.h) = contract. Reference: `compute_cov_kernel_scalar()`
in [`speed.c`](../speed.c): one running `double` sum, row-major, per element two
subtractions, one multiply, one add, each rounded alone. Product sits in own
statement + function-scoped no-contraction guard (GCC `optimize` attribute,
clang `fp contract(off)` pragma). Keep both on upstream sync: upstream body
`result += (val_x - mean_x) * (val_y - mean_y);` gets fused by clang on aarch64.

`compute_covariance_matrix()` walks lower triangle row by row of y blocks
(`compute_covariance_row()`); `speed_cov_row_fn` kernel returns up to
`SPEED_COV_ROW_MAX` (= block size 5) sums per call. Kernels:
`speed_cov_row_scalar` (count reference calls), `x86/speed_avx2.c`,
`x86/speed_avx512.c`, `arm64/speed_neon.c`. Lane = one sum; no lane feeds
another; no FMA. `test_speed_simd` (`memcmp`) on x86 and under qemu-aarch64.

Upstream `compute_cov_kernel_avx2` / `_avx512` (30f472b14) and `_neon`
(15297286) split one sum over lanes with FMA: not bit-exact, removed here.
**On upstream sync**: never re-import; port upstream changes to
`compute_covariance()` into `compute_covariance_row()` by hand.

Kernel reads up to `width + SPEED_COV_ROW_MAX - 1` floats per row of `data_y`
whatever `count`: SpEED's block grid always holds those floats. New caller -> same
guarantee or scalar kernel.
