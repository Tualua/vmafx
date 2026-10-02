---
paths:
  - core/src/feature/sycl/integer_*.cpp
  - core/src/feature/sycl/float_*.cpp
invariant: Kernel identities and output captures have an explicit boundary.
---
<!-- markdownlint-disable MD013 MD060 -->
# Kernel identities and output capture boundaries

- **Kernel identities and output captures have an explicit boundary**
  ([Research-2090](../../../../../docs/research/2090-sycl-silent-revert-residuals-2026-09-24.md)).
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
