---
paths:
  - core/src/feature/x86/ssim_avx2.c
  - core/src/feature/x86/ssim_avx512.c
  - core/src/feature/iqa/ssim_tools.c
  - core/src/feature/iqa/ssim_accumulate_lane.h
invariant: Scalar tails are plain C and need contraction off to preserve bit-exact scalar pairing.
---
# SSIM Accumulate and Scalar Reduction Invariants

| Group | TUs that move in lockstep |
| --- | --- |
| **SSIM accumulate** (ADR-0139) | `ssim_avx2.c` + `ssim_avx512.c` + `../arm64/ssim_neon.c` + scalar `../iqa/ssim_tools.c` (`ssim_accumulate_default_scalar`) + shared helper `../iqa/ssim_accumulate_lane.h`. Scalar tails (`sigma -= mu * mu`, `rm * rm + cm * cm + C1`) are plain C: need contraction off (ADR-1415; icx fused 13 of them in `ssim_avx512.c`, `float_ms_ssim` 1 fp32 ulp off on ~1 frame in 30). Guard: `test_ssim_x86_simd` (bit-exact vs transcribed scalar, counts with and without tail). |

- [ADR-0139](../../../../../docs/adr/0139-ssim-simd-bitexact-double.md) —
  SSIM accumulate per-lane scalar-double reduction.
