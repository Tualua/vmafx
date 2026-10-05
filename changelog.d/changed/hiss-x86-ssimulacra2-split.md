- **The x86 SSIMULACRA 2 kernels meet the HISS-04 size limits.** The 11
  functions over 60 lines in `ssimulacra2_avx2.c`, `ssimulacra2_avx512.c` and
  `ssimulacra2_host_avx2.c` (the XYB conversion, the SSIM and edge-difference
  maps, both blur passes and the picture-to-linear-RGB conversion) are split
  into static helpers with the same intrinsics, FMA pattern and summation order.
  Scores are byte-identical at every dispatch level (scalar, AVX2, AVX-512) on
  the Netflix pair and both 1080p checkerboard pairs at `--precision max`. The
  HISS baseline loses those 11 infractions.
