---
paths:
  - core/src/feature/sycl/sycl_compat.h
  - core/src/feature/sycl/float_motion_sycl.cpp
  - core/src/feature/sycl/float_adm_sycl.cpp
invariant: Kernel sub-group size: 16 or 32 only (ADR-1468); Xe2 AOT targets reject 8.
---
<!-- markdownlint-disable MD013 MD060 -->
# Kernel sub-group size policy

- **Kernel sub-group size: 16 or 32 only (ADR-1468).** Xe2 AOT targets
  reject 8; `sycl_compat.h` static_asserts it. The row kernels
  (`launch_float_motion_row_sad`, `float_adm` `launch_row_sums`,
  `launch_vif_row_sums`) and the `ssimulacra2` walk (`SS2S_WALK_SG`)
  required 8 for speed and are at 16: same bits (each is one sequential
  loop per work-item), scratch-free on A380, <= 2 % slower at 4K. Never
  restore 8 "for the A380": the default build compiles for 19 targets.
  Guards and the suite to run: [../../sycl/AGENTS.md](../../../sycl/AGENTS.md).
  Unverified at 16 on Xe2 / Xe-LP devices:
  `T-SYCL-ROW-KERNELS-SG16-OTHER-DEVICES-2026-10-02`.
