---
paths:
  - core/src/feature/cuda/integer_adm_cuda.c
  - core/src/feature/cuda/integer_cambi_cuda.c
invariant: cuLaunchKernel kernelParams must point to device-pointer value, not buffer struct.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# cuLaunchKernel kernelParams device-pointer value contract

- **`cuLaunchKernel` `kernelParams[]` must point to device-pointer
  VALUE, not to `VmafCudaBuffer` struct** (Issue lusoris/vmaf#857 /
  lusoris/vmaf#866).
  Since ADR-1379 / ADR-1380 cambi + SpEED kernels take ONE argument
  struct by value (device pointers as `uint64_t` fields) and host passes
  `void *params[] = {&args}` (`cambi_launch()`, `speed_launch()`). Passing
  `(void *)buf` (address of struct) makes driver read
  `buf->size` (host byte count) as device pointer, causing
  immediate GPU invalid-address fault (SIGSEGV/SIGBUS on host).
  Same invariant applies to every CUDA feature extractor that
  allocates device-side flat buffers via `vmaf_cuda_buffer_alloc`
  and passes them directly to `cuLaunchKernel`: always use
  `&buf->data`, never `(void *)buf`. Device-pointer arithmetic must
  also perform on `CUdeviceptr` integer type directly —
  avoid casting through `uint8_t *` (UB even though it round-trips
  on x86-64 today).
