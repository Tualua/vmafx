- **`float_ssim_hip` is bit-identical to the CPU `float_ssim` extractor.**
  The CPU adds the eleven products of a Gaussian window in `double` and
  rounds once per pass. The HIP twin added them in single precision, which
  rounds at every tap: on a gfx1036, 27 of 178 measured frames had the CPU's
  score and the others were up to 4.8e-7 away (5.4e-7 in the `enable_lcs`
  contrast and structure means). The twin now forms its window sums and its
  luminance, contrast and structure terms through the arithmetic
  `float_ms_ssim_hip` already shares with the CPU: all 178 frames from
  480x270 to 3840x2160 at 8 to 16 bits are identical at `--precision max`,
  with `enable_lcs`, `scale`, `enable_db` and `clip_db` too. The parity gate
  compares the CPU and HIP `float_ssim` cells with tolerance 0. It costs
  time: 2.0 ms instead of 1.7 ms per 1920x1080 frame and 5.2 ms instead of
  4.9 ms per 3840x2160 frame at the default scale, and a third more with
  `scale=1` (23.4 ms instead of 17.7 ms at 1080p). Stored `float_ssim_hip`
  scores change by up to 4.8e-7
  ([ADR-1441](docs/adr/1441-hip-float-ssim-cpu-window-sums.md),
  [HIP backend](docs/backends/hip/overview.md#float_ssim_hip-at-1080p-and-4k)).
