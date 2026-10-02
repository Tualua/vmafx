---
paths:
  - core/src/feature/simd_dx.h
invariant: simd_dx macro names are ISA-suffixed on purpose; never collapse them into cross-ISA aliases.
---
# simd_dx Macros and SIMD Widening Framework

## simd_dx macros (ADR-0140)

[`../simd_dx.h`](../../simd_dx.h) is fork-internal. AVX2 / AVX-512 paths
in this directory consume `SIMD_WIDEN_ADD_F32_F64_AVX2` /
`SIMD_WIDEN_ADD_F32_F64_AVX512`, `SIMD_ALIGNED_F32_BUF_*`,
`SIMD_LANES_*` to encode ADR-0138 / 0139 patterns by
construction. Macro names are ISA-suffixed on purpose; never
collapse them into cross-ISA aliases — fork's SIMD policy
rules out Highway / simde / xsimd (user memory
`feedback_simd_dx_scope.md`).

- [ADR-0140](../../../../../docs/adr/0140-simd-dx-framework.md) —
  `simd_dx.h` framework.
