---
paths:
  - core/src/feature/hip/float_psnr_hip.c
  - core/src/feature/hip/float_psnr_hip.h
  - core/src/feature/hip/float_psnr/float_psnr_score.hip
invariant: float_psnr_hip reproduces CPU reference bits identically.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# float_psnr_hip = CPU bits (ADR-1440, `EXACT_TWINS`)

Score = `float_psnr.c`'s bits at 8 / 10 / 12 / 16 bits (gfx1036: 178 of 178
frames, full-range noise included). Rebase-sensitive:

- CPU: float square of each difference, added in `double` = exact sum. Twin
  exact only if its sums are exact too.
- Kernel term: `fpsnr_square()` = `(uint32_t)((float)(ref - dis) *
  (float)(ref - dis))` = CPU term x scaler^2 (same mantissa; at 16 bits both
  round the square to 24 bits). Not an integer `d * d`: differs at 16 bits.
- Sums: `uint32` per wave and per block. <= 12 bits: one sum
  (256 * 4095^2 < 2^32). 16 bits (`bpc > 12u`): low 16 bits and the rest
  added separately, each < 2^24. Two `uint32` per block read back
  (`FPSNR_PARTIALS_PER_BLOCK`).
- Host: `lo + 65536 * hi` per block in `double`, sum, `/ (scaler * scaler)`,
  `/ n_pix`. All exact below 2^53 units.
- Never a float or double accumulator on the device: fp32 rounds at 10+ bits
  once a block's rms difference reaches 256 codes (was up to 7.6e-8 dB off);
  fp64 is exact but costs 2x frame time on gfx1036 (double shuffle = two).
- 16-bit bound from the CPU: its running sum rounds above 2^37 on the 8-bit
  scale (MSE > 16570 at 4K); twin returns the exact sum there.
- Guards: `test_hip_float_psnr_parity` + `_large` (device, `==`, noise at
  four depths), `test_hip_float_psnr_exact_contract.py` (five planted
  regressions).
