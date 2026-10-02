- **`float_ms_ssim_cuda` adds the terms of every scale in the CPU's order**
  (ADR-1465). CPU `float_ms_ssim` adds the luminance, contrast and structure
  value of every window into three double-precision sums per scale, from the
  first window to the last; the twin added the same values in blocks, and on
  rare frames (four in 8.3 million noise frames) a per-scale mean rounded to
  the neighbouring `float`, once moving the score in its tenth digit. The
  device now stores every window's values and the host adds them in order,
  so `float_ms_ssim` and the fifteen `enable_lcs` outputs equal `--backend
  cpu` on those frames and on every measured one. Cost: about 2.1 ns per
  scored window: 0.33 to 0.81 ms at 576x324, 2.7 to 8.8 ms at 1920x1080,
  10.9 to 33.7 ms at 3840x2160
  (`T-CUDA-FLOAT-MS-SSIM-EXACT-THROUGHPUT-2026-10-02`).
