- **Ten HIP test files conform to clang-tidy and HISS standards (part 2).**
  The second batch of HIP test sources (`test_hip_wavefront_reduce.c`,
  `test_hip_psnr_hvs_parity.c`, `test_hip_speed_chroma_parity.c`,
  `test_hip_speed_temporal_parity.c`, `test_hip_speed_singular_parity.c`,
  `test_hip_ciede_parity.c`, `test_hip_motion_parity.c`,
  `test_hip_vif_parity.c`, `test_hip_psnr_parity.c`, `test_hip_adm_parity.c`)
  were brought to zero clang-tidy findings in the HIP lane (-156 baseline
  findings) and cross-lanes under
  [ADR-1142](docs/adr/1142-clang-tidy-debt-ratchet.md). Functions exceeding
  branch and statement thresholds were split into clean sub-helpers satisfying
  HISS-04 / NASA JPL Rule 4. C23 `nullptr` diagnostics are scoped under ADR-1138
  to maintain MSVC C portability, and `__HIP_PLATFORM_AMD__` is supplied by the
  build harness (ADR-1263). All tests pass on AMD gfx1036.
