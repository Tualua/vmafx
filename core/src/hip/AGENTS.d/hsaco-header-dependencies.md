---
paths:
  - core/src/meson.build
  - core/src/hip/meson.build
invariant: All HIP HSACO custom targets bind depend_files and compiler depfiles covering include closures.
---
# HIP HSACO Kernel Header Dependency Tracking

- **HIP HSACO kernel header dependency tracking**
  ([ADR-1320](../../../../docs/adr/1320-cuda-hip-kernel-header-dependency-tracking.md);
  [Research-2106](../../../../docs/research/2106-cuda-hip-kernel-header-dependency-tracking.md)):
  All HIP HSACO custom targets (`hip_hsaco_*`) in `core/src/meson.build`
  must bind `depend_files: hip_kernel_shared_headers` covering complete
  repo-local quoted include closure, combined with compiler depfiles
  (`depfile: name + '.hsaco.d'` and
  passing `-Xclang -dependency-file -Xclang @DEPFILE@ -Xclang -MT -Xclang @OUTPUT@`
  to hipcc).
  Editing structs in shared headers (e.g. `integer_adm_cuda.h`, `vif_cuda.h`, `moment_cuda.h`)
  must reliably trigger incremental HSACO rebuilds in Ninja without manual `touch`
  workarounds. Do not remove `depend_files` or omit shared headers on rebase.
