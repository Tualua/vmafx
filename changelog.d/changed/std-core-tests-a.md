- **Ten core C and C++ test files conform to clang-tidy and HISS standards (part 1).**
  The first batch of CPU and core test sources (`test_barten_csf.c`,
  `test_psnr_hvs_simd.c`, `test_propagate_metadata.c`, `test_thread_pool.c`,
  `test_context.c`, `test_predict.c`, `test_ciede.c`,
  `test_pic_preallocation.c`, `test_locale_handling.c`, and `test_dict.cpp`)
  were refactored to zero clang-tidy findings in the CPU lane under
  [ADR-1142](docs/adr/1142-clang-tidy-debt-ratchet.md). Functions exceeding
  branch, nesting, and statement thresholds were split into modular helpers
  satisfying HISS-04 / NASA JPL Rule 4, eliminating 2 recorded infractions from
  `.standards-baseline.json`. C23 `nullptr` diagnostics in C files are scoped under
  [ADR-1138](docs/adr/1138-c23-nullptr-msvc-compat.md), and `test_dict.cpp` uses
  anonymous namespaces and standard `nullptr`. All tests continue to pass.
