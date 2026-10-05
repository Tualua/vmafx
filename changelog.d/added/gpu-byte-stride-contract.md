- **A static check keeps every GPU source from advancing a wide sample pointer
  by a byte stride.** `test_gpu_byte_stride_contract` (fast suite, no device)
  scans the CUDA, HIP, SYCL and Metal sources under `core/src`. It fails on a
  pointer to samples wider than a byte that is offset or indexed by a stride
  counted in bytes, the defect that made an upstream CUDA motion kernel read
  every other row of 16-bit input. Two SYCL kernels whose stride counts
  elements above scale 0 now name that unit. See
  [row addressing above 8 bits](docs/development/gpu-backend-template.md#row-addressing-above-8-bits).
