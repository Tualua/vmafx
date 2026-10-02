- **`float_ms_ssim` on HIP adds the windows of every scale in the CPU's
  order, so the scores are the CPU's on every frame.** The CPU extractor adds
  the luminance, contrast and structure terms of every window of a scale into
  one `double` each, left to right and top to bottom, and rounds each mean to
  `float`. The HIP twin (`integer_ms_ssim_hip`) computed the same terms
  (ADR-1403) and added them per wave and per 16x8 block. The sums differ in
  their last bits, which the rounding to `float` hides except on a mean next
  to a rounding boundary: on one constructed 176x176 noise frame the twin
  returned `float_ms_ssim_c_scale1` = 0.9854983687400818 where the CPU
  returns 0.9854984283447266, and `float_ms_ssim` differed by 1.3e-9. The
  twin now stores the three terms of every window and the host adds each
  scale in the CPU's order. On a gfx1036 at `--precision max` that frame
  returns the CPU's value on all 16 outputs, and 178 of 178 frames from
  480x270 to 3840x2160 at 8 to 16 bits stay identical, 2848 of 2848 values
  with `enable_lcs=true`. Cost on the gfx1036, medians of 32 interleaved
  pairs of runs: 37.3 to 42.7 ms per 1920x1080 frame and 183.1 to 200.8 ms
  per 3840x2160 frame, and 24 bytes of device and pinned host memory per
  window (65 MB at 1920x1080, 262 MB at 3840x2160)
  ([HIP backend](docs/backends/hip/overview.md#the-per-scale-sums-are-added-in-the-cpus-order-2026-10-02),
  construction of [ADR-1438](docs/adr/1438-hip-ssim-cpu-frame-sum.md)).
