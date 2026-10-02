- **`float_ssim_hip` adds the frame's windows in the CPU's order, so the
  score is the CPU's on every frame.** The CPU `float_ssim` extractor adds
  the term of every window into one `double`, left to right and top to
  bottom, and rounds the mean to `float`. The HIP twin computed the same
  terms (ADR-1441) and added them per 16x16 block. The two sums differ in
  their last bits, which the rounding to `float` hides except on a mean next
  to a rounding boundary: on one constructed 64x64 frame the twin returned
  -4.222829659283889e-07 where the CPU returns -4.222829943500983e-07, one
  `float` step apart. The twin now stores the term of every window and the
  host adds the plane in the CPU's order, for `float_ssim` and for
  `float_ssim_l`, `float_ssim_c` and `float_ssim_s` under `enable_lcs`, at
  every `scale`. On a gfx1036 at `--precision max` that frame returns the
  CPU's bits, and 178 of 178 frames from 480x270 to 3840x2160 at 8 to 16
  bits stay identical, 712 of 712 values with `enable_lcs=true`, at `scale=1`
  and `scale=3` too. Cost: none measurable at the automatic scale (2.31 to
  2.45 ms per 1920x1080 frame and 5.86 to 5.86 ms per 3840x2160 frame, inside
  the spread between runs); with an explicit `scale=1`, 2 to 5 ms more per
  1920x1080 frame (19.6 to 22.0 ms; 22.7 to 27.7 ms with `enable_lcs=true`)
  and 8 bytes of device and pinned host memory per window and sum
  ([HIP backend](docs/backends/hip/overview.md#the-frame-sum-is-added-in-the-cpus-order-2026-10-02),
  construction of [ADR-1438](docs/adr/1438-hip-ssim-cpu-frame-sum.md)).
