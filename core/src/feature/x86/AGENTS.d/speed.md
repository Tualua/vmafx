---
paths:
  - core/src/feature/x86/speed_avx2.c
  - core/src/feature/x86/speed_avx512.c
  - core/src/feature/speed.c
  - core/src/feature/speed_cov.h
invariant: One lane equals one covariance sum, multiply then add, no FMA.
---
# SpEED Covariance Row SIMD Kernels

| Group | TUs that move in lockstep |
| --- | --- |
| **SpEED covariance row kernels** (ADR-1459) | `speed_avx2.c` + `speed_avx512.c` + `../arm64/speed_neon.c` + scalar `compute_cov_kernel_scalar` and `speed_cov_row_scalar` in `../speed.c`; contract in `../speed_cov.h`, dispatch via `SpeedState::cov_row` (`speed_dispatch_cpu_kernel`). Bit-exact: every sum equals `compute_cov_kernel_scalar` (`../../test/test_speed_simd.c`, `memcmp`). One lane = one covariance sum (x block against up to five y blocks at consecutive columns), multiply then add, no FMA. Never split one sum over lanes and never fuse: upstream kernels `compute_cov_kernel_avx2` / `_avx512` (30f472b14) and `_neon` (15297286) do both and stay out on sync. Change kernel signature or `SPEED_COV_ROW_MAX` -> change all four implementations + test in same PR. |
