- **Eleven core C and C++ test files conform to clang-tidy and HISS standards (part 2).**
  The second batch of CPU and core test sources (`test_feature_collector.c`,
  `test_luminance_tools.cpp`, `test_framesync.c`, `test_log.c`, `test_psnr.c`,
  `test_version.c`, `test_adm_csf.c`, `test_cpu.c`, `test_ref.c`,
  `test_moment_simd.c`, and `tiny_ai_test_template.h`) were brought to zero
  clang-tidy findings on the CPU lane (-17 recorded findings) under
  [ADR-1142](docs/adr/1142-clang-tidy-debt-ratchet.md). Functions exceeding
  branch and statement thresholds were split into modular helpers satisfying
  HISS-04 / NASA JPL Rule 4. C23 `nullptr` diagnostics in C files are scoped under
  [ADR-1138](docs/adr/1138-c23-nullptr-msvc-compat.md), and all files retain valid
  SPDX license identifiers. All tests continue to pass.
