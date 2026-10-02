- **The SYCL clang-tidy lane measures the x86 SIMD and DNN sources again.**
  Since the strict floating-point arguments became project-wide (ADR-1461),
  151 translation units of an icx build carry them twice, and stock clang
  answers the repeat with a driver warning that has no source location. The
  ratchet treats such a warning as an unusable measurement, so
  `make tidy-ratchet LANE=sycl` stopped with exit 4 and no baseline entry of
  those files could be tightened. `scripts/ci/clang-tidy-sycl.sh` silences
  that one driver note (`-Wno-overriding-option`); the compile commands and
  every check stay as they were
  (`T-SYCL-TIDY-OVERRIDING-OPTION-2026-10-02`).
