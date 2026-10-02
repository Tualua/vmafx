- **`float_ssim_cuda` adds its frame sums in the CPU's order** (ADR-1464).
  CPU `float_ssim` adds every window's value into one double-precision sum
  from the first window to the last; the twin added the same values in
  blocks, and on rare frames (two in 31 million noise frames) the mean
  rounded to the neighbouring `float`. The device now stores every window's
  value and the host adds them in order, with `enable_lcs` the luminance,
  contrast and structure sums too, so `float_ssim` and `float_ssim_l`, `_c`,
  `_s` equal `--backend cpu` on every input: 9828 of 9828 measured values and
  880 000 of 880 000 on noise, the constructed frame of
  `core/test/float_ssim_order_frame.h` included. Cost: about 1 ns per scored
  window. Nothing measurable at the automatic scale of 1080p and 4K input;
  where the picture is scored at full size, 0.14 to 0.33 ms at 576x324 and
  3.9 to 12.6 ms for 3840x2160 with `scale=1`
  (`T-CUDA-FLOAT-SSIM-EXACT-THROUGHPUT-2026-10-02`).
