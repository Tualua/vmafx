- The ten products that CodeQL's `cpp/integer-multiplication-cast-to-long`
  reported after the upstream-parity reverts (ADR-1475, ADR-1476, ADR-1488) in
  `ciede.c`, `third_party/xiph/psnr_hvs.c`, `x86/psnr_hvs_avx2.c`,
  `arm64/psnr_hvs_neon.c`, `integer_adm_kernels.h`, `adm_tools.h` and `iqa/convolve.c` now
  write the conversion of the product's result explicitly, as
  `sqrt((double)(a * b))`. The arithmetic and the object code are unchanged;
  the query reports only implicit widenings, and the `// codeql[...]` comments
  they carried do not suppress in this repository's CodeQL setup.
