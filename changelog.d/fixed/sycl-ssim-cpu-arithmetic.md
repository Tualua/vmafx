- **`integer_ssim_sycl` returns the CPU's `ssim` bit for bit, and works on
  16-bit input.** The CPU `ssim` extractor computes each pixel's term in
  `double` and adds all terms in one running sum. A SYCL kernel has no
  `double`, and the twin used `float` and added per block, which left the
  score up to 3.1e-7 from the CPU (2.0e-4 with `enable_db`) and made 16-bit
  frames fail with `invalid ratio`. The kernel now performs the CPU's
  `double` operations in 64-bit integers and the host adds the terms in the
  CPU's order ([ADR-1443](docs/adr/1443-sycl-ssim-cpu-arithmetic.md)).
  Measured on an Arc A380 at `--precision max`, every frame is identical on
  the Netflix 576x324 pair at 8, 10, 12 and 16 bits, both 1080p checkerboard
  pairs and 200 frames of BBB 3840x2160, with `enable_db` and `clip_db` too.
  A 3840x2160 frame takes 31.9 ms instead of 17.8 ms (0.78 instead of 0.45 ms
  at 576x324), and the twin holds 66 MB more pinned host memory at that size.
  The parity gate compares this twin with tolerance 0. Stored
  `integer_ssim_sycl` scores change by up to 3.1e-7.
