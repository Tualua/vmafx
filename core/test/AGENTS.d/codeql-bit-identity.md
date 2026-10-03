---
paths:
  - core/test/float_bits.h
  - core/test/test_float_bits.c
  - core/test/*_twin_parity.h
  - core/test/test_metal_*_parity.c
  - core/test/test_speed_upstream_form.c
invariant: Exact results are compared with float_bits.h, never `==` or a new memcpy copy; test variants keep static bodies equal.
---
<!-- markdownlint-disable MD013 -->
# Bit identity of floating-point results (ADR-1502)

A test that asserts a result has another computation's bits (a twin against
the CPU extractor, a replay against the reference, a recorded value) uses
`core/test/float_bits.h`:

- `vmaf_test_identical_f64(a, b)` / `_f32`: same bit pattern and not a NaN.
  It is strictly stronger than `==` (a ±0 mismatch fails, a NaN still fails),
  so swapping it in never weakens a test. Do not "simplify" it back to `==`,
  to `memcmp()`, or to a bit compare without the NaN rule:
  `test_float_bits` fails for each of those.
- `vmaf_test_expect_identical_f64(what, a, b)` / `_f32`: the same, and one
  line with both values at `%.17g` and their bits on a mismatch. Use it where
  the assertion prints nothing of its own.
- A test that shows two forms differ (the fixture tells the fork's form from
  a wrong one) negates the helper. A comparison with a constant (a sentinel,
  a literal) stays `==`; CodeQL `cpp/equality-on-floats` does not report it.
- Do not add a private `memcpy` bit helper to a test. 40 files still carry
  one from before ADR-1502; convert a file's copy when you edit the file.

A test source compiled into two meson variants (for example
`test_speed_upstream_form` and its `_foreign_libm` twin) must keep the body
of every static function the same in both: put the difference in a
file-scope constant, not in an `#if` inside a function. CodeQL keys a static
function by its body, so an `#if` gives the two variants two functions of one
name, and it reports the one the merged `run_tests()` does not call as
unreachable (`cpp/unused-static-function`, alert 1367).
