- **`ciede_cuda` computes the CPU's arithmetic and agrees with it to 1e-11.**
  The CUDA twin of `ciede` was up to 1.1e-5 from the CPU extractor and
  matched it on no frame. The CPU computes CIEDE2000 in double precision and
  stores intermediate values in `float`; the twin computed everything in
  `float`, with another form of the formula, and added per 16x16 block. The
  twin now evaluates the CPU's expressions in the CPU's types and adds the
  per-pixel values on the host in the CPU's order
  ([ADR-1426](docs/adr/1426-cuda-ciede-cpu-arithmetic.md)). Measured on an
  RTX 4090 at `--precision max`: 62 of 113 frames identical to the CPU and
  the rest within 1.4e-11 (Netflix 576x324 at 8, 10, 12 and 16 bits, both
  1080p checkerboard pairs, BBB 3840x2160). What is left is the math library:
  the CPU calls glibc, the device CUDA's functions, and a few pixels per
  million round to the neighbouring `float` (38 of 8.3 million on a 4K frame,
  because glibc's `powf` is not correctly rounded). The parity gate compares
  this twin at `1e-9` instead of `5e-3`. The price is time: a 3840x2160 frame
  takes 32.7 ms instead of 2.8 ms and a 576x324 frame 0.74 ms instead of
  0.34 ms, because the per-pixel math is now double precision; the CPU
  extractor takes 222 ms per 4K frame on sixteen threads. Stored `ciede_cuda`
  outputs change by up to 1.1e-5. The SYCL, HIP and Metal twins keep their
  `float` arithmetic and the `5e-3` tolerance.
