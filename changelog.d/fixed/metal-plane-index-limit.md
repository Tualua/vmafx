- **The Metal `float_vif`, integer SSIM, `float_ssim` and `float_ms_ssim`
  twins refuse a frame whose moment planes pass their 32-bit index.** They
  index five planes of N samples in `uint`, which wraps from N = 858,993,460
  (past 16K, or 16K with `vif_prescale` above about 2.544); the planes past the
  wrap aliased the start of the buffer. `init()` now fails with `-EINVAL` and
  names the limit; 16K at the default options is accepted as before
  ([Metal backend](docs/backends/metal/index.md#largest-frame-of-the-five-plane-twins)).
