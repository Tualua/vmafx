- **`float_psnr_hip` is bit-identical to the CPU `float_psnr` extractor at
  every bit depth.** The CPU adds the squared sample differences in `double`,
  which is exact. The HIP twin added each 16x16 block in single precision,
  which is exact at 8 bits and rounds at 10, 12 and 16 bits once the
  differences in a block are large. On real clips the two agreed; on
  full-range noise the twin was 6.3e-9 dB off at 10 bits, 2.5e-8 at 12 and
  1.8e-8 at 16, and 7.6e-8 on a bright 16-bit 1080p pair. The twin now adds
  the same squares as integers: 178 of 178 measured frames from 480x270 to
  3840x2160 at 8 to 16 bits are identical at `--precision max` on a gfx1036
  (167 before), with `uncapped=true` too, at the same time per frame. The
  parity gate compares the CPU and HIP `float_psnr` cells with tolerance 0.
  Stored `float_psnr_hip` scores of high-bit-depth clips with heavy
  distortion change by up to 7.6e-8 dB
  ([ADR-1440](docs/adr/1440-hip-float-psnr-exact-block-sums.md),
  [PSNR](docs/metrics/psnr.md#float_psnr)).
