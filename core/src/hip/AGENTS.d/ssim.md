---
paths:
  - core/src/feature/hip/float_ssim_hip.c
  - core/src/feature/hip/float_ssim_hip.h
invariant: float_ssim_hip mirrors CUDA twin with two dispatches per frame and five intermediate float buffers.
---
# Float SSIM Multi-Dispatch and Buffer Pyramid

- **`float_ssim_hip.c` mirrors `integer_ssim_cuda.c`
  call-graph-for-call-graph** (fork-local, ADR-0274). State struct
  carries five `uintptr_t` intermediate float buffer slots
  (`h_ref_mu`, `h_cmp_mu`, `h_ref_sq`, `h_cmp_sq`, `h_refcmp`)
  tracked outside kernel-template's readback bundle; runtime PR
  (T7-10b) will swap them for real device-buffer handles matching
  CUDA twin's `VmafCudaBuffer *h_*` field shape. Extractor reports
  `chars.n_dispatches_per_frame == 2` (first multi-dispatch HIP
  consumer); smoke test pins this value explicitly. v1 `scale=1`
  constraint surfaces as `-EINVAL` at init time before
  kernel-template's `-ENOSYS` would surface, mirroring CUDA twin's
  `compute_scale` / `vmaf_log` validation. HIP twin extracts
  `validate_dims_hip` / `init_dims_hip` helpers from `init()` to fit
  `readability-function-size` budget — CUDA twin keeps everything
  inline. **On rebase**: keep five-slot count and
  `n_dispatches_per_frame == 2` characteristic aligned with CUDA
  twin; do not re-inline helpers without verifying budget still
  passes.
