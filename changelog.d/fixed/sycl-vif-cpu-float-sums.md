- **`vif_sycl` rounds its per-scale sums where the CPU does, and emits the
  CPU's default outputs.** The CPU `vif` extractor stores each scale's
  numerator and denominator sum in a `float` and divides in single precision.
  The SYCL twin kept the sums in `double`, so every score of every frame was
  up to 3.5e-7 from the CPU. It now rounds at the same points: measured on an
  Arc A380 at `--precision max`, the denominator sums are identical on every
  frame, and the scores on 12 to 41 of the 48 Netflix 576x324 frames
  (depending on the scale) and on 140 to 196 of 200 BBB 3840x2160 frames,
  where none was before. The remaining frames are one or a few `float` steps
  off in a numerator (at most 3.6e-7 in a score), because the kernel computes
  the per-pixel gain in `float`. The twin's `debug` option now defaults to
  `false`, as on the CPU; it defaulted to `true` and added eleven debug
  outputs to every run. Request them with `--feature vif_sycl=debug=true`.
  Stored `vif_sycl` scores change by up to 3.5e-7.
