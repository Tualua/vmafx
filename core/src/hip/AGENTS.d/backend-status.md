---
paths:
  - core/src/hip/common.c
  - core/src/hip/kernel_template.c
invariant: HIP backend landed across phased milestones with audit-first scaffold and real module API consumers.
---
# Backend Status and Consumer Milestones

## Backend status

Scaffold + first through eighth consumers — landed across multiple PRs.
Two additional consumers promoted from scaffold to real kernels in
ADR-0372 (batch-1, this PR).

1. **T7-10 audit-first scaffold** (ADR-0212) — common, picture, dispatch,
   feature stubs, public header `libvmaf_hip.h`, CI lane (now `Ubuntu HIP`,
   required check), smoke-only `enable_hip` build.
   Every public C-API entry point returns `-ENOSYS`.
2. **T7-10 first consumer** (ADR-0241) — `kernel_template.{h,c}` (mirror
   of `cuda/kernel_template.h`) + `feature/hip/integer_psnr_hip.{c,h}`
   (first kernel-template consumer) + `vmaf_fex_psnr_hip` registration
   under `#if HAVE_HIP`. Template helpers and consumer's
   submit/collect return `-ENOSYS` until T7-10b.
3. **T7-10b second consumer** (ADR-0254, PR #324) —
   `feature/hip/float_psnr_hip.{c,h}` mirroring
   `feature/cuda/float_psnr_cuda.c`. Float partials precision posture.
4. **T7-10b third + fourth consumers** (ADR-0259 / ADR-0260, PR #330) —
   `ciede_hip` (`submit_pre_launch` bypass shape) and
   `float_moment_hip` (four-uint64 atomic-counter readback shape).
5. **T7-10b fifth + sixth consumers** (ADR-0266 / ADR-0267, PR #340) —
   `feature/hip/integer_motion_v2_hip.{c,h}`. (`float_ansnr_hip.{c,h}`
   was fifth consumer per ADR-0266 but was removed in commit 70ed8b3ce3
   / PR #38; only `integer_motion_v2_hip` remains from this batch.)
   Pin (b) temporal-extractor shape with `flush()` callback +
   ping-pong buffer carry.
6. **T7-10b seventh + eighth consumers** (ADR-0273 / ADR-0274) —
   `feature/hip/float_motion_hip.{c,h}` and
   `feature/hip/float_ssim_hip.{c,h}`. Pin (a) three-buffer
   ping-pong plus `motion_force_zero` short-circuit posture, (b)
   multi-dispatch shape (`chars.n_dispatches_per_frame == 2`).
7. **T7-10b runtime landed** (2026-05-08) — `kernel_template.c` and
   `common.c` now wrap real HIP runtime calls
   (`hipStreamCreateWithFlags`, `hipEventCreateWithFlags`,
   `hipMemsetAsync`, `hipStreamWaitEvent`, `hipStreamSynchronize`,
   `hipMalloc` + `hipHostMalloc`, `hipFree` + `hipHostFree`,
   `hipGetDeviceCount`, `hipSetDevice`, `hipGetDeviceProperties`).
   `vmaf_hip_state_init` returns `0` on host with `>=1` AMD GPU;
   `-ENODEV` otherwise. `vmaf_hip_import_state` was implemented in
   ADR-0519 (2026-05-18), now lives in `core/src/libvmaf.c` next to
   CUDA / SYCL / Metal `_import_state` twins; stub body
   removed from `common.c`. Remaining feature-kernel ports follow as
   their own PRs gated by `places=4` cross-backend-diff lane
   (ADR-0214).
8. **Batch-1 real kernels** (ADR-0372) — `integer_psnr_hip` promoted
   from `-ENOSYS` scaffold to real `hipModuleLoadData` +
   `hipModuleLaunchKernel` consumer under `#ifdef HAVE_HIPCC`. Without
   `HAVE_HIPCC`, scaffold `-ENOSYS` contract preserved.
   (`float_ansnr_hip` was also promoted in ADR-0372 but subsequently
   removed in commit 70ed8b3ce3 / PR #38.)
9. **Batch-2 real kernel** (ADR-0373, this PR) — `float_motion_hip`
   promoted from `-ENOSYS` scaffold to real HIP module-API consumer.
   Adds `blur[2]` ping-pong + `ref_in` staging (`hipMalloc`) inside
   `#ifdef HAVE_HIPCC`; `compute_sad=0` on first frame; motion2 tail
   in `flush()`. Device kernel: `float_motion/float_motion_score.hip`.
