- **`float_ssim` on frames smaller than its 11x11 window is 0 on every CPU
  path, as in Netflix's libvmaf, and no longer reads or writes outside its
  workspace.** On a host with AVX2, AVX-512 or NEON an 8x8 frame scored
  values such as 0.46 or 0.81 that changed from run to run, and a frame of
  4x4 or smaller overran a heap buffer: the SIMD kernels were given the
  product of the two window extents, which is positive when both are
  negative. The scalar path (`--cpumask 63`) was always correct. Frames of
  11x11 and larger are unchanged.
