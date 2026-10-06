- **The CUDA kernels, their headers and two CUDA tests meet the clang-tidy
  standard.** The 62 files of the `cuda` lane that were not shared C headers
  owned by the `cpu` lane are cleaned: device pointers are rebuilt from a
  kernel argument's `CUdeviceptr` with one macro (`cuda_device_ptr.cuh`),
  helpers live in anonymous namespaces, the long VIF statistic and the
  SpEED / CAMBI / ADM / SSIM / PSNR-HVS kernels are split into helpers, and
  the shared device headers carry the ADR-1138 brackets for the C includers.
  No score moves: the CUDA twins return the same bits on the Netflix pair, both
  1080p checkerboard pairs and a 10-bit pair, and every CUDA device test passes.
