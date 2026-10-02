---
paths:
  - core/src/feature/hip/integer_adm_hip.c
  - core/src/feature/hip/hip_hsaco_stubs.c
invariant: HSACO symbol kernel keys must match host translation unit consumers exactly.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# HSACO symbol naming — kernel keys must match the host-TU consumer (ADR-0539)

HIP host TU referencing kernel module via
`hipModuleLoadData(..., <name>_hsaco)` -> `hip_kernel_sources` meson key MUST be exactly
`<name>` — `xxd -i -n <name>_hsaco` step inside meson custom_target
derives symbol from that key. Two gotchas:

1. **Meson key matches symbol name directly**:
   `float_moment_hip.c` consumes `moment_score_hsaco` via:

   ```meson
   # float_moment_hip.c consumes `moment_score_hsaco`
   'moment_score' : feature_src_dir + 'hip/float_moment/moment_score.hip',
   ```

   (Historical `integer_moment_score` duplicate key removed in
   ADR-1154 together with uncompiled
   `integer_moment/moment_score.hip` orphan).
2. **Missing meson registration produces undefined-reference link
   error** for `<name>_hsaco`, NOT runtime `-ENOSYS`. Seeing such
   link failure -> either register kernel (preferred) or add weak
   stub in `hip_hsaco_stubs.c` (per ADR-0536 — only for kernels that
   can't yet compile standalone via `hipcc --genco`).
