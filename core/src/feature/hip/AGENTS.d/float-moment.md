---
paths:
  - core/src/feature/hip/float_moment_hip.c
  - core/src/feature/hip/float_moment_hip.h
  - core/src/feature/hip/float_moment/moment_score.hip
invariant: float_moment_hip matches CPU bits below 2^53.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# float_moment_hip = CPU bits below 2^53 (ADR-1447)

- Exact twin `float_moment` (`scripts/ci/exact_twins.d/float_moment.hip`);
  gfx1036: 250 of 250 frames, four outputs each.
- CPU second moment: square in `float` (`pic_ * pic_`), added in `double`.
  16 bpc: float square = integer square rounded to 24 bits. Kernel adds
  `moment_float_square()` (one fp32 product -> integer < 2^32). Never
  `r * r` in integers at 16 bpc, never an fp64 square.
- 8 bpc kernel: integer square = float square (16 bits), unchanged.
- Sum = exact uint64 in units of 1 / scaler^2. Host: `(double)sum /
  scaler^2 / pixels`, CPU's two divisions, this order.
- CPU sum exact < 2^53 units: every frame <= 2^21 pixels, every 8 / 10 / 12
  bit frame. Past it (16 bit, moment * pixels >= 2^37) CPU rounds per add;
  twin within `(pixels - 2^21 + 1) / pixels * 2^(e - 69) + 2^-37`. Measured
  2.7e-7 (2560x1440, tenth of samples < 4096). Exact there = `ordered_sum.h`
  port, `T-HIP-FLOAT-MOMENT-PAST-2-53-2026-10-02`.
- CUDA / SYCL / Metal twins still add integer squares:
  `T-GPU-FLOAT-MOMENT-16BIT-SQUARES-2026-10-02`.
- Guards: `test_hip_float_moment_parity` (+ `_large`),
  `test_hip_float_moment_exact_contract.py`.
