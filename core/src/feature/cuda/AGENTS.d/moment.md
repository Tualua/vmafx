---
paths:
  - core/src/feature/cuda/integer_moment_cuda.c
  - core/src/feature/cuda/integer_moment_cuda.h
invariant: float_moment_cuda reproduces CPU float_moment bit for bit.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# `float_moment_cuda` = CPU `float_moment`, bit for bit (ADR-1453)

- CPU: `moment.c` squares each sample in `float`, adds floats into ONE
  double per output. Kernel (`integer_moment/moment_score.cu`): four
  `uint64` sums; second sums add `sample_square<T>()`: `uint16_t` ->
  `moment_float_square()` = one `__fmul_rn()` product of raw sample, as
  integer below 2^32 (= CPU term in units of 1 / scaler^2; = integer square
  up to 12 bit, rounded to 24 bits at 16); `uint8_t` -> integer square (same
  number). Host: CPU's two divisions (`moment_cuda_scaler()`).
- NEVER `r * r` in integers for 16bpc samples (1.0e-4 off at 16 bit), never
  fp64 square, never plain `*` (intrinsic = contract).
- Exact while sum < 2^53 units (every frame <= 2^21 pixels, every 8/10/12-bit
  frame); past 2^53 CPU's sum rounds per add, twin within derived bound
  (`T-HIP-FLOAT-MOMENT-PAST-2-53-2026-10-02`).
- 16-bit Netflix fixture = 8-bit shifted left, shows nothing: use full-range
  content.
- Guards: `test_cuda_float_moment_parity` (+ `_large`; cases in
  `core/test/float_moment_twin_parity.h`, shared with SYCL test),
  `test_cuda_float_moment_exact_contract.py`.
