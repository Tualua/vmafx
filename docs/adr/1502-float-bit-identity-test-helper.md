<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1502: Tests that assert an exact result compare floats by their bits through one helper, `core/test/float_bits.h`, which treats a NaN as identical to nothing

- **Status**: Accepted
- **Date**: 2026-10-03
- **Deciders**: lusoris
- **Tags**: `testing`, `codeql`, `floating-point`, `gpu-parity`, `rc3`, `fork-local`

## Context

The exact-twin parity tests (a CUDA, HIP, SYCL or Metal twin against the CPU
extractor), the replay tests (a device header against the reference) and the
recorded-value tests assert that a result has another computation's bits. On
2026-10-03, 59 of the 71 open CodeQL alerts were `cpp/equality-on-floats` on
the `==` / `!=` those tests use, and #1922 added three more sites on master.

`==` is not the test those files claim. It calls +0 and -0 equal, and a twin
can return one where the CPU returns the other; it calls a NaN unequal to
itself. Where a test needed the bits, it grew a private helper instead: 42
files under `core/test/` each carry their own `memcpy` into a `uint32_t` or
`uint64_t` (`uf_bits()`, `float_bits()`, `vif_twin_bits()`,
`hvs_score_bits()`, `scores_bit_identical()`, ...), and
[ADR-1308](1308-codeql-float-equality-contracts.md) added more of them per
file. HISS-19 asks for one implementation of one behaviour.

A pure bit comparison has its own trap: two NaNs with the same payload compare
equal, so a test that failed when both sides returned NaN would start to pass.

## Decision

`core/test/float_bits.h` is the one bit-identity helper of the C and C++
tests:

- `vmaf_test_identical_f64(a, b)` / `_f32` hold when `a` is not a NaN and the
  two bit patterns are equal. That is exactly `a == b` and the same bits, so
  the helper is never weaker than the `==` it replaces: a ±0 mismatch now
  fails, and a NaN still fails.
- `vmaf_test_expect_identical_f64(what, a, b)` / `_f32` do the same and print
  one line on a mismatch with both values at `%.17g` and their bits, for an
  assertion without its own report.
- `vmaf_test_bits_f64()` / `_f32()` return the bit patterns.

A site that asserts a result is another computation's uses the helper. A site
that asserts two forms differ (a fixture tells the fork's form from a wrong
one) uses its negation, the complement of the twin tests' criterion. A
comparison with a constant (a sentinel such as `expect_cpu == 0.0`, a literal
result) keeps `==`: CodeQL exempts constants, and the meaning there is
numeric. A file's private bit helper moves to the shared one when the file is
next touched; this change converts the two in files it edits (`uf_bits()` in
`test_speed_upstream_form.c`, `float_bits()` in two Metal parity tests).
`core/test/test_float_bits.c` holds the helper to its cases, and fails for the
two weaker forms (`a == b`, bits without the NaN rule).

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Keep `==` and dismiss the alerts as false positives | No code change | The tests keep accepting a ±0 mismatch, so they do not assert what they say; a dismissal needs the maintainer and returns on every line shift (alerts 81, 88 and 96 came back as 1341, 1338 and 1339) | Rejected |
| Pure bit comparison, NaN payloads compared | Simplest definition | Weaker than `==` on NaN: a twin and a CPU that both return the same NaN would pass | Rejected |
| `memcmp()` of the two objects | One call | Same NaN behaviour; clang-tidy reports `memcmp` on floating-point objects (`bugprone-suspicious-memory-comparison`) | Rejected |
| A helper per file (ADR-1308) | Local, no new header | 42 copies already; HISS-19 | Rejected |
| **One helper, same bits and not NaN** | Strictly stronger than `==`; one definition; reports the bits | ±0 differences that `==` hid now fail (none found on any device) | **Chosen** |

## Consequences

- **Positive**: every exact test asserts bits; a mismatch prints the bits; one
  definition for new tests.
- **Negative**: a twin that returns -0 where the CPU returns +0 fails where it
  passed. Measured on 2026-10-03: the CPU suite (GCC and clang), the CUDA
  (RTX 4090), HIP (gfx1036) and SYCL (Arc A380) parity tests that include the
  changed headers, and the Metal parity sources as self-tests, all pass.
- **Follow-ups**: the 40 private helpers left in files this change does not
  touch move to `float_bits.h` when those files are next edited.
  `core/test/AGENTS.d/codeql-bit-identity.md` carries the rule.

## References

- `req` (task brief, 2026-10-03): "these tests assert BIT identity, so the
  honest form is a bit comparison, which `==` is not (`==` says +0 == -0 and
  NaN != NaN). Add ONE shared helper for the C tests".
- CodeQL query `cpp/equality-on-floats` (`FloatComparison.ql`, cpp-queries
  1.8.3 / 1.9.0): comparisons with a constant operand are not reported.
- [ADR-1308](1308-codeql-float-equality-contracts.md),
  [ADR-1142](1142-whole-codebase-standards.md).
