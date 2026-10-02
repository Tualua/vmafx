---
paths:
  - core/test/test_cambi.c
  - core/test/test_svm_api.c
invariant: CodeQL flags float equality; test_cambi uses float_bits_equal; svm_labels_equal compares 64-bit IEEE bit identity.
---
<!-- markdownlint-disable MD013 -->
# Floating-point assertions and CodeQL contracts (ADR-1308)

CodeQL query `cpp/equality-on-floats` flags direct equality checks on floats.
In tests:

- In `test_cambi.c` (Alert 1244), `check_c_values_avx2_parity` uses
  `float_bits_equal(c_scalar[i], c_avx2[i])` comparing `uint32_t` bit patterns
  via `memcpy` to enforce bit-exact parity between AVX2 and scalar
  `calculate_c_values` paths. Do not revert to `c_scalar[i] == c_avx2[i]`.
- In SVM API tests (`test_svm_api.c`, Alert 1101), `svm_labels_equal(a, b)`
  compares discrete integer-class labels via 64-bit IEEE bit identity with
  signed-zero equivalence (`+0.0 == -0.0`) and same-infinity behavior, rejecting
  NaN (never equal). It does not use `a - b == 0.0` or finiteness checks.
