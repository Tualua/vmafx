- **`vif_sycl` returns the CPU's scores bit for bit.** The CPU `vif` extractor
  computes a pixel's gain in `double` and truncates two results to integers
  before its log2 table. A SYCL kernel has no `double`, and the twin used
  `float`, which put a share of those integers one off and left a scale's
  score up to 3.6e-7 from the CPU on some frames. The kernel now computes
  both integers exactly: one integer division decides them, and a pixel whose
  value lies within the `double` chain's own rounding error of an integer
  (one in 300 000) replays the CPU's operations in 64-bit integers
  ([ADR-1432](docs/adr/1432-sycl-integer-vif-exact-gain.md)). Measured on an
  Arc A380 at `--precision max`, every output of every frame is identical on
  the Netflix 576x324 pair at 8, 10, 12 and 16 bits, both 1080p checkerboard
  pairs and 200 frames of BBB 3840x2160, with `debug=true`, with
  `vif_enhn_gain_limit` of 1.0, 1.2 and 37.5, and for a clip scored against
  itself. A 3840x2160 frame takes 0.75 ms longer (21.46 to 22.21 ms). The
  parity gate compares this twin with tolerance 0. Stored `vif_sycl` scores
  change by up to 3.6e-7.
