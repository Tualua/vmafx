---
paths:
  - core/src/feature/sycl/integer_adm_sycl.cpp
  - core/src/feature/sycl/integer_vif_sycl.cpp
  - core/src/feature/sycl/integer_motion_sycl.cpp
  - core/src/feature/sycl/integer_motion_v2_sycl.cpp
  - core/src/feature/sycl/integer_cambi_sycl.cpp
  - core/src/feature/sycl/integer_moment_sycl.cpp
  - core/src/feature/sycl/integer_psnr_sycl.cpp
  - core/src/feature/sycl/integer_psnr_hvs_sycl.cpp
  - core/src/feature/sycl/float_adm_sycl.cpp
  - core/src/feature/sycl/float_motion_sycl.cpp
  - core/src/feature/sycl/float_psnr_sycl.cpp
  - core/src/feature/sycl/float_vif_sycl.cpp
  - core/src/feature/sycl/integer_ciede_sycl.cpp
  - core/src/feature/sycl/integer_ms_ssim_sycl.cpp
  - core/src/feature/sycl/integer_ssim_sycl.cpp
  - core/src/feature/sycl/speed_chroma_sycl.cpp
  - core/src/feature/sycl/speed_temporal_sycl.cpp
  - core/src/feature/sycl/ssimulacra2_sycl.cpp
  - core/test/test_sycl_zero_copy_admission.c
  - core/test/test_sycl_zero_copy_model_gate.c
invariant: reads_shared_luma_only() true only if submit() reads no host picture, only the shared planes (ADR-1688, ADR-1768).
---
<!-- markdownlint-disable MD013 MD060 -->
# Zero-copy admission hook

- **`reads_shared_luma_only()` tells the truth** ([ADR-1688](../../../../docs/adr/1688-sycl-zero-copy-luma-only-admission.md), widened by [ADR-1768](../../../../docs/adr/1768-sycl-zerocopy-chroma-admission-post-1-0.md)).
  `vmaf_read_pictures_sycl()` passes no picture; the import fills the shared
  luma and chroma planes (ADR-1765). It admits an extractor only when the hook
  answers true. Since ADR-1768 every SYCL twin reads only the shared planes and
  answers true for any options; a chroma reader whose input carried no chroma
  is refused at `submit()` by `vmaf_sycl_require_chroma()`, not by the hook.
  The name is ADR-1688's (a rename is a follow-up). **On rebase / change**: a
  twin whose `submit()` starts reading `ref_pic` / `dist_pic` drops its hook
  (answers false) in the same PR; a new SYCL twin gets one only if it reads the
  shared planes alone. Designated initializer goes after `.provided_features`,
  before `.chars` / `.context_check` (C++ declaration order). Guards:
  `test_sycl_zero_copy_admission` (every SYCL extractor's answer, device-free),
  `test_sycl_zero_copy_model_gate` (refusals, `vmaf_v0.6.1` and luma twins ==
  CPU), `test_sycl_zerocopy_parity` (chroma twins == CPU on emulated import).
