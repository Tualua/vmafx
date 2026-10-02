---
paths:
  - core/src/feature/sycl/sycl_compat.h
  - core/src/feature/sycl/integer_adm_sycl.cpp
  - core/src/feature/sycl/integer_vif_sycl.cpp
invariant: No scratch memory in kernels; zero private_mem_size and spill_memory_size on xe.
---
<!-- markdownlint-disable MD013 MD060 -->
# Scratch memory avoidance on Intel Xe

- **No scratch memory in kernels ([ADR-1395](../../../../../docs/adr/1395-sycl-kernels-no-scratch.md)).**
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
