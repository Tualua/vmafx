---
paths:
  - core/src/feature/x86/moment_avx2.c
  - core/src/feature/x86/moment_avx512.c
  - core/src/feature/float_moment.c
invariant: Do not change the sequential per-lane double accumulation order without updating parity tests.
---
# float_moment SIMD Reduction Invariants

| Group | TUs that move in lockstep |
| --- | --- |
| **float_moment SIMD** (ADR-0179 / ADR-0987) | `moment_avx2.c` + `moment_avx512.c` + `../arm64/moment_neon.c` + `../arm64/moment_sve2.c` + scalar `../float_moment.c`. Pure reduction kernels — no inter-pixel dependence, so bit-exactness contract is tolerance-bounded (1e-7 relative, not byte-exact); tested in `../../test/test_moment_simd.c`. The AVX-512 path (`HAVE_AVX512` gate, `compute_1st/2nd_moment_avx512`) widens the 8-lane AVX2 path to 16-lane ZMM. Do NOT change the sequential per-lane `double` accumulation order without updating the tolerance and the parity tests. |
