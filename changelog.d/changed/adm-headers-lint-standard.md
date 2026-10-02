- **The ADM headers are at the lint standard (ADR-1142).**
  `core/src/feature/adm_tools.h`, `adm_csf_tools.h`, `adm_options.h` and
  `integer_adm.h` report no clang-tidy finding on the cpu, cuda, hip, sycl and
  arm64 lanes (145, 145, 145, 156 and 145 before). `adm_tools.h` no longer
  carries the nine `ADM_CM_THRESH_S_*` macros of upstream, which nothing has
  expanded since the closed form `adm_cm_thresh3x3_s()` replaced them
  (ADR-1141). No score changes: every object file of an x86 and of an aarch64
  build is byte-identical before and after.
