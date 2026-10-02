---
paths:
  - core/src/feature/cuda/integer_adm_cuda.c
  - core/src/feature/cuda/integer_psnr_cuda.c
invariant: Pinned-host memory must be freed in close_fex and destroy_fex after readback_free.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# Pinned-host memory lifetime and readback_free ownership

- **`vmaf_cuda_kernel_readback_free` owns pinned-host free
  (2026-05-29 sweep).** Helper in `core/src/cuda/kernel_template.h`
  calls `vmaf_cuda_buffer_host_free(cu_state, rb->host_pinned)` before
  NULLing pointer. Callers of `vmaf_cuda_kernel_readback_free` must
  NOT also call `vmaf_cuda_buffer_host_free` on `rb->host_pinned` — doing
  so would double-free pinned allocation. Pre-2026-05-29 pattern
  where callers called `vmaf_cuda_buffer_host_free` explicitly =
  incorrect; helper now owns free. See
  PR fix/cuda-pinned-host-leak-sweep-20260529.
- **Feature-local pinned host buffers must be freed in BOTH `close_fex_cuda`
  AND init `free_buffers` error path (2026-06-27 bug-hunt).** Buffers
  allocated directly via `vmaf_cuda_buffer_host_alloc` (i.e. NOT routed through
  `vmaf_cuda_kernel_readback_free`) — e.g. `float_vif_cuda::rows_host`,
  `float_adm_cuda::accum_host`, `integer_ms_ssim_cuda::h_ref`/`h_cmp`/`h_*_partials`
  — owned by feature, must be released with
  `vmaf_cuda_buffer_host_free` (no separate `free()`, unlike device-buffer
  wrappers which need both). Missing close-path free leaks page-locked host
  memory on every `vmaf_close()`. See branch fix/bughunt-cuda.

## Pinned-host memory free invariant after `readback_free` (ADR-0754)

- **`vmaf_cuda_kernel_readback_free` NULLs `rb->host_pinned` but does NOT free it.**
  Kernel template explicitly documents this as caller responsibility (see
  comment in `cuda/kernel_template.h` near `vmaf_cuda_kernel_readback_free`). Every
  `close_fex_cuda` that calls `readback_free` must save `rb.host_pinned` to local
  BEFORE calling `readback_free`, then call `vmaf_cuda_buffer_host_free(cu_state,
  saved)` afterward. Omitting host-free leaks one page of CUDA pinned host
  memory per `vmaf_close()` cycle. `integer_ssim_cuda.c::close_fex_cuda` =
  reference fix (ADR-0754). Verify with:
  `compute-sanitizer --tool memcheck --leak-check full ./vmaf --feature float_ssim --backend cuda ...`
  — summary must show 0 bytes from `cuMemHostAlloc` after fix.
  Note: `integer_psnr_cuda.c` has same gap, scheduled for follow-up.
