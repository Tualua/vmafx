- **`speed_chroma` and `speed_temporal` return Netflix's values, and their
  GPU twins return the CPU's bit for bit.** The fork's port of the SpEED
  extractors had three single-precision forms where Netflix computes in
  double precision and rounds once (`1.0 / sqrt(1 + t * t)` in the Givens
  rotation, the `log2()` of the entropy and of the score). Against a build of
  Netflix/vmaf the CPU scores differed on most frames: `speed_chroma` by up
  to 2.3e-5, `speed_temporal` by up to 6.6e-4, the `vmaf_v1.0.16` models by
  up to 2.5e-5. `speed.c` carries Netflix's expressions again
  ([ADR-1477](docs/adr/1477-speed-upstream-double-math.md)): every value
  compared is identical (261 frames per `speed_chroma` output, 320
  `speed_temporal` frames, 204 scores per `vmaf_v1.0.16` model, 3564 option
  values), with the scalar kernels, the default dispatch and AVX2, and GCC
  and clang builds for x86-64 and aarch64 agree. The fork still differs
  where it decided to: `speed_max_val` clamps `speed_temporal` too, a
  prescale above 1 no longer reads past a buffer, and a frame too small for
  SpEED is refused. The CUDA, HIP and SYCL twins now run `speed.c` on the
  device up to the per-block variances and form the entropies and the score
  on the host with `speed.c`'s own statements, so they call the logarithm the
  CPU extractor calls: on an RTX 4090, a gfx1036 and an Arc A380 all 3409
  values compared equal `--backend cpu` of the same build, GCC or icx, where
  a GCC build's CPU and a twin used to differ on a few outputs by up to
  1.9e-6. The parity gate compares the six cells with tolerance 0 (`5e-6`
  and `4e-5` before). No twin is measurably slower. Stored `speed_chroma`,
  `speed_temporal` and `vmaf_v1.0.16` scores, CPU or GPU, change in the
  digits above; re-run them before comparing at full precision.
