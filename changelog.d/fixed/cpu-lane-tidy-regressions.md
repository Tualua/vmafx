- **Brought CPU lane back to clang-tidy baseline.** Resolved 13 clang-tidy
  regressions introduced by merges into `core/src/picture_pool.cpp`,
  `core/src/read_json_model.cpp`, `core/test/test_psnr_hvs_score.c`,
  `core/test/test_read_pictures_failure_ownership.c`, and `core/tools/vmaf.cpp`
  without baseline modifications or `NOLINT` waivers. Refactored callbacks,
  sign comparisons, test helpers, and assertion guards to preserve exact
  behavior and numerical equivalence across all CPU test suites.
