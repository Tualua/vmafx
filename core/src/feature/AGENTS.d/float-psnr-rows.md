---
paths:
  - core/src/feature/float_psnr_rows.h
invariant: float_psnr twins add each row's exact sum into a double in the CPU's row order.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# float_psnr's sum of rows (ADR-1499)

- `float_psnr_rows.h::vmaf_float_psnr_row_noise()`: the CUDA, SYCL and HIP
  hosts' only form of `float_psnr.c::extract()`'s `noise_` (before `/ (w * h)`),
  in units of 1 / scaler^2: each row's segment sums added in `uint64`, the
  rows added into ONE double, row 0 first.
- Why it is the CPU's bits on every input: a row's terms are integers below
  2^32, a row has at most 2^15, so every partial sum inside a row is exact
  in a double in any order (every `noise_line()` variant); only the adds of
  the rows round (past 2^53 units), and the helper makes the same adds of
  the same values in the same order; a power-of-two scale changes nothing.
- The kernels' blocks / work-groups must each lie in ONE row (256 x 1).
  NEVER add segments of different rows before the double add, never round a
  frame total once, never reorder rows.
- A change to `float_psnr.c`'s row loop or `noise_line()` (e.g. a float
  accumulator) changes this header, `test_float_psnr_rows` and the three
  twins in the same PR.
