---
paths:
  - core/src/feature/hip/speed_hip_pipeline.c
  - core/src/feature/hip/speed_hip_pipeline.h
  - core/src/feature/hip/speed/speed_pipeline.hip
invariant: SpEED singular-covariance contract and device-resident CPU fp32 arithmetic flags must hold.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# SpEED singular-covariance contract (ADR-1202, ADR-1218)

25x25 SpEED covariance matrix regular only if **every** eigenvalue
at least `1e-6`. CPU treats singular one as routine numerical
condition, not failure; the device chain (ADR-1384) matches it on two
counts.

1. **Zero solution on device.** `speed_hd_block_statistics()` starts
   every block's solution at 0 and solves only when the channel's
   `status` slot says regular, so singular channel scores from zero
   solution, never from previous frame's memory.
2. **Singularity travels out-of-band.** Per-channel `singular` flags
   ride in `SpeedGpuFrameResult`; errors are return codes.
   `speed_hd_score_finish()` applies `speed_extract_score()`'s rule
   (score `0` when exactly one of ref/dis singular) on device;
   `speed_chroma_hip.c::combine_chroma_uv()` imputes
   `speed_chroma_uv` from surviving channel on host, from flags only.

Guarded by `core/test/test_hip_speed_singular_parity.c`. Older
`test_hip_speed_temporal_parity.c` fixture is 768x432, whose chroma
planes give 4x2 = 8 blocks for 25x25 covariance — singular on every
frame — never exercises regular path. SpEED test needing regular frame
must be at least 960x960 and textured: `test_hip_speed_chroma_parity`
uses the 960x960 splatter fixture of `speed_chroma_twin_parity.h`
(ADR-1452).

## SpEED device-resident: CPU fp32 arithmetic by build flag (ADR-1384)

- `speed_chroma_hip` / `speed_temporal_hip` run ADR-1358 chain through one
  pipeline, `speed_hip_pipeline.{h,c}`. Twins: configure, bindings, upload,
  submit, collect. No `hipModuleLaunchKernel`, no sync, no host SpEED stage
  (`picture_copy`, `speed_internal_filter_and_downscale`, eigen / QR helpers)
  in twin TUs or pipeline.
- Per frame: `speed_hip_pipeline_upload()` (staged, no wait), eight kernels,
  one `SpeedGpuFrameResult` copy. `speed_hip_pipeline_collect()` /
  `_wait()` = only wait (`vmaf_hip_kernel_collect_wait`).
- Init-time geometry, taps, scoring = `speed_internal_gpu_configure()`
  (`speed_internal.c`), shared with SYCL; types = `speed_gpu_common.h`.
  Parameter block = `speed_hip_params_fill()` / `speed_hip_taps_fill()` /
  `speed_hip_bindings_*()`; replay test calls same.
- Exact arithmetic = `hip_strict_fp_args` in `core/src/meson.build`
  (`-ffp-contract=off`, `-fhip-fp32-correctly-rounded-divide-sqrt`; every
  kernel, ADR-1407) + plain `*` `+` `/` `sqrtf()`. Never `__fmul_rn` / `__fadd_rn` / `__fdiv_rn` / `__fsqrt_rn`:
  without `OCML_BASIC_ROUNDED_OPERATIONS` = plain (contracting) operators /
  native approximate sqrt. Explicit `fmaf()` only for exact two-product.
- log2 = `speed_hd_log2_rn()` (fp32 pairs, one rounding). Host libm `log2f`
  only behind `SPEED_HD_HOST_LIBM_LOG2 && !__HIP_DEVICE_COMPILE__` (test
  seam). glibc `log2f` misrounds ~0.4 %: vs glibc CPU a few chroma frames
  differ in last bits; compare with correctly rounded `log2f` preload.
  Measured (ADR-1452, gfx1036, glibc 2.44): 13 of 990 `speed_chroma` values
  off, 1.4e-6 max, 0 with preload. Gate cell =
  `LIBM_TWINS["speed_chroma"]["hip"]` 5e-6 (scores < 16);
  `test_hip_speed_chroma_parity` = three scores, every frame, relative 1e-6
  (`core/test/speed_chroma_twin_parity.h`, shared with the CUDA test). New
  difference there = twin regression until the preload run says otherwise.
  Never port glibc's `log2f` to the device.
- No fp64 in `speed/`. lanczos4 prescale weights = host table
  (`speed_hip_upload_lanczos()`, `speed_internal_gpu_lanczos_weights()`,
  CPU scaler's own routine), 9 taps per scaled column then per scaled row,
  read through `SpeedHipParams::lanczos`. No device sine: fp32 `sinpif`
  weights were 8.8e-3 relative off the CPU on smooth content
  (T-GPU-SPEED-LANCZOS4-PRESCALE-DRIFT-2026-09-30).
- Guards: `test_hip_speed_device_math` (replay vs CPU extractor),
  `test_hip_device_resident_contract.py`, `test_hip_speed_*_parity` on
  device.
