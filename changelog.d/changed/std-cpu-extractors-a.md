- **Twelve CPU feature extractor and support files conform to clang-tidy and HISS standards (batch 1).**
  The first batch of CPU feature extractors and support headers (`integer_motion.c`,
  `integer_motion.h`, `convolution_internal.h`, `barten_csf_tools.h`, `blur_array.c`,
  `float_vif.c`, `math_utils.h`, `ms_ssim.c`, `ssim.c`, `integer_motion_v2.c`,
  `feature_collector.h`, and `ssim_tools.c`) were refactored to zero clang-tidy findings
  in the CPU lane under [ADR-1142](docs/adr/1142-clang-tidy-debt-ratchet.md). Functions
  exceeding NASA JPL Rule 4 complexity and line-count limits in `integer_motion_v2.c`
  were split into modular helpers, eliminating 2 recorded infractions from
  `.standards-baseline.json`. All 12 reference score configurations remain bit-identical
  at `--precision max` across scalar, AVX2, and AVX-512 cpumasks, and the Netflix CPU
  golden gate passes with unchanged counts.
