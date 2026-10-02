- **Eight more feature sources and tests meet the lint and HISS standard and
  carry their SPDX line.** `core/src/feature/integer_ssim.c`: `calc_ssim()`
  (73 lines, the file's one HISS row) takes its two kernels and its row ring
  from `ssim_work_init()` / `ssim_work_free()`, in the allocation order of the
  Xiph.Org original, and loses its `NOLINT`. `core/test/test_feature_collector.c`:
  the two CUDA-only tests are split into helpers with every assertion kept, and
  the duplicate-owner test releases its fixtures before it asserts on the
  releases (one analyzer leak finding). `feature_collector.cpp`,
  `luminance_tools.h`, `moment.c`, `sycl/integer_adm_sycl.cpp`,
  `test_framesync.c` and `test_luminance_tools.cpp` already measured zero and
  only gain the line; their stale allowances in the CUDA, HIP and SYCL tidy
  baselines are removed. No score changes: `ssim`, `float_moment`, `cambi` and
  the default model are identical at `--precision max` under scalar, AVX2,
  AVX-512 and NEON dispatch, and the `adm` SYCL twin is identical to the CPU on
  333 frames ([ADR-1142](docs/adr/1142-whole-codebase-standards.md),
  [ADR-1250](docs/adr/1250-eupl-fork-relicense.md)).
