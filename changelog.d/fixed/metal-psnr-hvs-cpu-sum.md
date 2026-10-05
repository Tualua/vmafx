- **`integer_psnr_hvs_metal` computes the CPU's `psnr_hvs` scores.** The
  Metal twin added the 64 masked coefficient errors of each 8x8 block on the
  device and the host added the block sums, where the CPU extractor adds every
  error of a plane into one running `float`. It also formed the masking table
  as an fp32 product, which rounds 98 of its 192 entries differently from the
  CPU's `double` product. An outside tester's Apple M4 Pro (issue #2118)
  measured `psnr_hvs` up to 1.7e-3 dB from the CPU on the 1080p checkerboard
  pair and 191 of 192 values different on the Netflix 576x324 pair. The kernel
  now stores every error, and the host adds them in the CPU's order with the
  helpers the CUDA, HIP and SYCL twins use. The host forms the masking table in
  `double`, since Metal has no `double`
  ([ADR-1397](docs/adr/1397-psnr-hvs-twins-cpu-float-sum.md),
  [ADR-1498](docs/adr/1498-metal-twins-exact-designs.md)). Checked on Linux:
  the kernel's arithmetic, compiled as C, equals the CPU extractor on every
  output of the parity test's fixtures and the Netflix and checkerboard pairs.
  Nothing has run on an Apple device yet; the macOS tester bundle runs
  `test_metal_integer_psnr_hvs_parity` and the parity gate for that.
