- **Eleven CPU feature extractor and support files conform to clang-tidy standards (batch 2).**
  The second batch of CPU feature extractors and support headers (`speed_internal.h`,
  `feature_characteristics.h`, `motion.c`, `float_ssim.c`, `float_ms_ssim.c`,
  `vif_tools.h`, `vif_options.h`, `psnr_options.h`, `picture_copy.h`,
  `picture_copy.cpp`, and `null.c`) were refactored to zero clang-tidy findings
  in the CPU lane under [ADR-1142](docs/adr/1142-clang-tidy-debt-ratchet.md). All 12
  reference score configurations remain bit-identical at `--precision max` across
  scalar, AVX2, and AVX-512 cpumasks, and the Netflix CPU golden gate passes with
  unchanged counts.
