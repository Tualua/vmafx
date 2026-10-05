- **`apsnr_*` is right on long, heavily distorted clips.** The `psnr`
  extractor and its CUDA, HIP, SYCL and Metal twins summed each plane's
  squared error over the clip in an unsigned 64-bit integer. At 16 bits with
  every sample at the maximum difference that sum wrapped at frame 2072 of
  1080p, 122 of 8K and 33 of 16K (at 12 bits at 530,502, 31,085 and 8,290),
  and `apsnr_*` came out several dB too high. The sum is 128 bits wide now;
  clips that never reached 2^64 keep their values.
