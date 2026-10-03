---
paths:
  - core/src/feature/cuda/float_psnr_cuda.c
  - core/src/feature/cuda/float_psnr_cuda.h
  - core/src/feature/cuda/float_psnr/float_psnr_score.cu
invariant: float_psnr_cuda matches CPU float_psnr bit for bit, past 2^53 units too.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# `float_psnr_cuda` = CPU `float_psnr`, bit for bit (ADR-1455, ADR-1499)

- CPU: `float_psnr.c` squares each difference in `float`, adds squares in
  double per row (always exact: < 2^47 units of 1 / scaler^2), rows in
  double (rounds past 2^53 units, in row order).
- Kernel (`float_psnr/float_psnr_score.cu`): `fpsnr_square()` = one
  `__fmul_rn()` product of raw sample difference, as integer below 2^32
  (= CPU term times scaler^2). Warp + block sums = `unsigned long long`;
  one `uint64` per block read back. Block = `FPSNR_BX` (256) pixels of ONE
  row (`FPSNR_BY` 1, `float_psnr_cuda.h`); NEVER blocks that span rows. ONE body `fpsnr_block<T>()` for both
  kernels; same seven arguments (ADR-1215), no `bpc`.
- Host: `float_psnr_noise()` = `vmaf_float_psnr_row_noise()`
  (`feature/float_psnr_rows.h`): each row's blocks in `uint64`, rows into a
  double in order (ADR-1499), then / scaler^2 (power of two), / pixel count
  = CPU's operations. NEVER one frame total rounded once.
- NEVER fp32 warp / block sums (exact at 8 bit only; 1.2e-7 dB off at 10 to
  16 bit with large differences), never scale difference before squaring,
  never integer square (16 bit: CPU's square = rounded to 24 bits).
- High-bit-depth Netflix fixtures = 8-bit shifted left, show nothing: use
  full-range content.
- Guards: `test_cuda_float_psnr_parity` (+ `_large`; cases in
  `core/test/float_psnr_twin_parity.h`, shared with SYCL test),
  `test_cuda_float_psnr_exact_contract.py`.
