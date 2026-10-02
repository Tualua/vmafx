---
paths:
  - core/src/feature/x86/motion_avx2.c
  - core/src/feature/x86/motion_avx512.c
  - core/src/feature/x86/float_motion_avx2.c
  - core/src/feature/x86/float_motion_avx512.c
invariant: motion_v2_avx2.c using logical shift is knowingly out-of-spec vs scalar; do not port to NEON.
---
# Motion v2 SIMD Divergence Notes

| Group | TUs that move in lockstep |
| --- | --- |
| **Motion v2 NEON / AVX2 divergence** (ADR-0145) | `motion_v2_avx2.c` (currently uses `_mm256_srlv_epi64` *logical*) is **knowingly out-of-spec** vs scalar; `../arm64/motion_v2_neon.c` matches scalar via arithmetic shift. Do NOT port the AVX2 logical pattern to NEON. The AVX2 audit is a separate batch. |
