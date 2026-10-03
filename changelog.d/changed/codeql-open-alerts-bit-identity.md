- **The exact-twin, replay and recorded-value tests compare floating-point
  results by their bits ([ADR-1502](docs/adr/1502-float-bit-identity-test-helper.md)).**
  `core/test/float_bits.h` holds when two values have the same bit pattern and
  are not NaN, which is stricter than the `==` the tests used: `==` accepted
  +0 for -0. With it, 69 of the 73 open CodeQL alerts are fixed in code
  (`cpp/equality-on-floats`, `cpp/integer-multiplication-cast-to-long`,
  `cpp/missing-header-guard`, `cpp/commented-out-code`,
  `cpp/unused-static-function`). No library or tool object file changes (GCC
  and clang, x86-64 and aarch64), and no score moves.
