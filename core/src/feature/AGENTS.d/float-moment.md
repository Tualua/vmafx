---
paths:
  - core/src/feature/moment.c
  - core/src/feature/moment.h
invariant: compute_2nd_moment reduction order and floating-point accumulation contracts.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# moment.c compute_2nd_moment Reduction Contract

- **`moment.c::compute_2nd_moment` reduction contract** (ADR-0179 / ADR-0987):
  `const float term = pic_ * pic_; cum += (double)term;` where float squaring
  is evaluated in single precision and explicitly cast to `double` before
  accumulation into `cum`. Unlike `convolve.c`
  ([ADR-0138](../../../../docs/adr/0138-iqa-convolve-avx2-bitexact-double.md),
  bit-exact), `moment.c` is governed by
  [ADR-0179](../../../../docs/adr/0179-float-moment-simd.md) (AVX2/NEON) and
  [ADR-0987](../../../../docs/adr/0987-avx512-float-moment.md) (AVX-512) under a
  **tolerance-bounded non-byte-exact reduction contract**
  (`MOMENT_REL_TOL = 1e-7`), verified by `test_moment_simd`. Decoupling the
  float product into intermediate `term` and explicit `(double)` cast
  removes the CodeQL `cpp/integer-multiplication-cast-to-long` source pattern
  behind historically dismissed Alert 707 (
  [Research-2031](../../../../docs/research/2031-codeql-float-widening-multiplication.md))
  by eliminating compiler-generated widening conversions. **On rebase:** do not
  pre-widen operands (`(double)pic_ * pic_`) or revert to direct implicit
  widening (`cum += pic_ * pic_`).
