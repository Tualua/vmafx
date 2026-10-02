---
paths:
  - core/src/feature/cuda/integer_ciede_cuda.c
  - core/src/feature/cuda/integer_ciede_cuda.h
invariant: ciede_cuda performs CPU arithmetic with libm apart.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# `ciede_cuda` = CPU arithmetic, libm apart (ADR-1426)

- **`integer_ciede/ciede_device.h` = `ciede.c` statement for statement.**
  CPU computes fp64, stores float; header keeps each type:
  `get_lab_color()` fp64 to the cube root, `xyz_to_lab_map()` returns
  float; `ciede2000()` intermediates `const float` from fp64 expressions.
  Genuinely float: sums / differences of floats, the three final
  quotients, `get_r_sub_t()`'s `powf` ratio and `powf(degrees, 2)`,
  `degrees_to_radians()`'s float argument. Kernel is C++: a float argument
  picks the float overload of `sqrt` / `atan2` / `sin` / `cos` / `exp`, so
  EVERY promotion is spelled `(double)`. `pow(x, 2)` = `ciede_sq()` (exact
  fp64 product; glibc returns it). No `cbrtf` / `atan2f` / `sinf` / `cosf`
  / `expf` / `sqrtf`.
- **`CIEDE_POWF`**: host = glibc `powf` (the CPU's call), device =
  `(float)pow((double)x, (double)y)` (correctly rounded). glibc `powf` is
  NOT correctly rounded (0.07 % of `x^7`, 0.16 % of `x^2` arguments): 38 of
  8.3M pixels per 4K frame differ by one float step. That + fp64 libm
  last-place differences (1 px) = whole residual, 1.4e-11. Do not swap in
  CUDA `powf` (4 ULP).
- **No device reduction.** `extract()` = one double accumulator, raster
  order; kernel stores one float per pixel (`terms[y * width + x]`), host
  `ciede_frame_sum()` (`../ciede_frame_sum.h`, one definition for the CUDA,
  SYCL and HIP hosts; `ciede_device.h` includes it for host code). Old fp32
  warp / block sums alone = 1.4e-9 at 4K.
- Gate: `LIBM_TWINS["ciede"]["cuda"] = 1e-9`, NOT `EXACT_TWINS`. Open:
  `T-CUDA-CIEDE-LIBM-RESIDUAL-2026-10-01` (glibc `powf` port would remove
  38 of 39 px), `T-CUDA-CIEDE-EXACT-THROUGHPUT-2026-10-01` (32.7 ms per 4K
  frame vs 2.8: fp64 transcendentals; skip identical triples = exact 0).
- Guards: `test_ciede_device_math` (host replay == CPU extractor, bit for
  bit, incl. a frame whose sum rounds), `test_cuda_ciede_exact_contract.py`,
  `test_cuda_ciede_parity` (1e-8 at 256x144).
- Channel reads keep the ADR-0762 pattern below.
