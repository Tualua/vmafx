- **The sycl clang-tidy lane is at zero findings (ADR-1142).**
  `core/src/sycl/common.cpp` and eight SYCL tests (`test_sycl.c`, `test_sycl_pic_preallocation.c`,
  `test_sycl_kernel_scratch.c`, `test_sycl_kernel_registration.c`,
  `test_sycl_shared_frame_sticky_geometry.c`, `test_sycl_vif_min_dim.c`,
  `test_integer_cambi_sycl.c`) report no finding, and `scripts/ci/tidy-baseline-sycl.json` is empty.
  The `VMAF_SYCL_PROFILE`, `VMAF_SYCL_TIMING`, `VMAF_SYCL_IMPORT_DEBUG` and `VMAF_SYCL_CHECKSUM`
  switches are now read once at first use through `vmaf_gpu_dispatch_env_get()`, like
  `VMAF_SYCL_DISPATCH` (documented in `docs/api/gpu.md`). The generated sRGB EOTF table
  `ssimulacra2_eotf_lut.h` (and its generator) carries a cited `NOLINT` block. No score changes.
