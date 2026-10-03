- **`float_ms_ssim` with `enable_chroma` runs on CUDA and HIP, and HIP no
  longer drops the chroma scores.** The HIP twin `integer_ms_ssim_hip`
  accepted `enable_chroma=true`, scored luma only and wrote neither
  `float_ms_ssim_cb` nor `float_ms_ssim_cr`, without a warning: a HIP run
  that set the option has no chroma scores. The CUDA twin had no such option,
  so such a request ran on the CPU. Both twins now run the luma pipeline once
  per plane and return the CPU extractor's three scores bit for bit
  (`--precision max`, RTX 4090 and gfx1036: 1080p checkerboards, Netflix
  576x324 at 4:2:2 10 bit and 4:4:4 8 and 10 bit, BBB 1080p 4:4:4 and BBB 4K
  4:2:0, with `enable_lcs`, `enable_db` and `clip_db` too), and refuse, as
  the CPU does, a frame whose chroma is smaller than 176 pixels. The parity
  gate has a `float_ms_ssim_chroma` cell, exact on CUDA, HIP and SYCL. See
  [MS-SSIM](docs/metrics/ms-ssim.md).
