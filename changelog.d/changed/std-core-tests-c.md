- **Core test files brought to lint and HISS standards (batch C).**
  The final batch of core C test files (`core/test/test.h`,
  `core/test/test_float_adm_dwt2_neon.c`, `core/test/test_float_adm_neon.c`,
  `core/test/test_float_motion_neon.c`, `core/test/test_motion_neon.c`,
  `core/test/test_psnr_neon.c`, `core/test/test_ssim_neon.c`,
  `core/test/test_gpu_picture_pool.c`, `core/test/test_integer_cambi_sycl.c`)
  was brought to full compliance under [ADR-1142](docs/adr/1142-clang-tidy-debt-ratchet.md).
  `test.h` modernized C++ typedefs to type aliases (`using`) and eliminated redundant
  void parameter lists. Missing SPDX license identifiers were restored per
  [ADR-1250](docs/adr/1250-eupl-fork-relicense.md). C23 `nullptr` diagnostics are
  scoped under [ADR-1138](docs/adr/1138-c23-nullptr-windows-portability.md) to maintain
  MSVC cl.exe compatibility across all architecture blocks. `test_pelorus_interop.c`
  is tracked as an exact-sync vendored Pelorus ABI mirror per
  [ADR-1113](docs/adr/1113-vendor-pelorus-interop.md).
