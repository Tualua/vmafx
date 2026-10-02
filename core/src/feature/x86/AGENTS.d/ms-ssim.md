---
paths:
  - core/src/feature/x86/ms_ssim_decimate_avx2.c
  - core/src/feature/x86/ms_ssim_decimate_avx512.c
  - core/src/feature/ms_ssim_decimate.c
invariant: The 9-tap filter table appears verbatim in all four; diff all four when any one moves.
---
# MS-SSIM Decimate Separable SIMD LPF

| Group | TUs that move in lockstep |
| --- | --- |
| **MS-SSIM decimate LPF** (ADR-0125) | `ms_ssim_decimate_avx2.c` + `ms_ssim_decimate_avx512.c` + `../arm64/ms_ssim_decimate_neon.c` + scalar `../ms_ssim_decimate.c`. The 9-tap filter table appears verbatim in all four — diff all four when any one moves. |

- [ADR-0125](../../../../../docs/adr/0125-ms-ssim-decimate-simd.md) —
  MS-SSIM decimate separable SIMD.
