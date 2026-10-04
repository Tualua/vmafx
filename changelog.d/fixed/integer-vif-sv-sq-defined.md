- **Integer VIF no longer converts an out-of-range `double` to `int32_t`
  ([ADR-1561](docs/adr/1561-integer-vif-sv-sq-defined.md)).** The residual
  variance of the gain model, `sigma2_sq - g * sigma12`, reaches about -2^45,
  and Netflix's code converted it to `int32_t` before clamping it at 0, which
  is undefined behaviour below INT32_MIN: x86 returns INT32_MIN (so 0 after
  the clamp), an aarch64 build that vectorises the loop keeps the low 32 bits.
  `vif_sv_sq()` returns x86's value on every target and the scalar statistic,
  the AVX2 and NEON helpers and the CUDA and HIP kernels call it. No score
  changes: the Netflix pair and both checkerboard pairs give the same values
  at every x86 dispatch level, and clang's `-fsanitize=undefined` no longer
  stops `test_integer_vif_sv_sq`.
