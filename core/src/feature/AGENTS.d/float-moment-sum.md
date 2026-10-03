---
paths:
  - core/src/feature/float_moment_sum.h
  - core/src/feature/float_moment_sum_gpu.h
invariant: float_moment twins form the CPU's rounded 2nd-moment sum past 2^53 units; integers only, checked walk.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# The CPU's second-moment sum past 2^53 units (ADR-1497)

- `float_moment_sum.h`: one header, CUDA + SYCL + HIP kernels and
  `test_float_moment_sum`. Units of 1 / scaler^2: every CPU step = exact
  integer sum rounded to 53 bits, ties to even (`vmaf_moment_sum_add_term()`).
  Rows: exact sum, plan from exact prefix (advice), increments under the plan
  (`vmaf_ordsum_round_shifted()` + `vmaf_ordsum_then()`, ordered tree), walk
  (`vmaf_moment_sum_walk_rows()`); failed row -> 256 runs under the sum's
  binade and the next (`vmaf_moment_sum_walk_runs()`); failed run -> terms.
- Integers only: NO `double` / `float` in the header (SYCL kernels have no
  fp64, ADR-0220; `test_float_moment_sum_contract.py` scans it).
- Lane steps need `VMAF_MOMENT_SQUARE(v)` = the twin's own
  `moment_float_square()`; never an integer square at 16 bit.
- `float_moment_sum_gpu.h`: the four CUDA / HIP kernel bodies
  (`moment_row_totals_body()` ...); `moment_score.cu` / `moment_score.hip`
  define the `extern "C" __global__` kernels of the same names around them
  (names resolved by `cuModuleGetFunction` / `hipModuleGetFunction`; a
  non-static definition in a header is a tidy finding). Each returns at once
  while the plane's exact sum <= 2^53 (it is the CPU's then). Walk writes
  `sums[2 + plane]`, host `collect()` unchanged.
- Never trust the plan: every add from increments goes through
  `vmaf_moment_sum_add_run()` at the exact sum. Never reorder the tree
  (composition not commutative). Never drop the term fallback.
- A change to `compute_2nd_moment()`'s order or term changes this header and
  `test_float_moment_sum` in the same PR. Host frame limit 2^30 pixels keeps
  every sum < 2^62.
