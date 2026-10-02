---
paths:
  - core/src/libvmaf.c
  - core/src/feature/feature_extractor.cpp
invariant: Extractor flag-promotion is per-extractor and gated on verified end-to-end CLI reproducer within places=4.
---
# HIP Extractor Flag Promotion and Dispatch Routing

- **HIP extractor flag-promotion is per-extractor and gated on
  verified end-to-end CLI reproducer** (fork-local, ADR-0530 —
  supersedes ADR-0241 "flag bit reserved but cleared" invariant for
  `vmaf_fex_psnr_hip` and friends). Flag bit (`1 << 6`) IS now set on
  extractors that have verified-working real HIP kernel — currently
  only `vmaf_fex_integer_motion_hip` qualifies. `vmaf_fex_psnr_hip`,
  `vmaf_fex_ciede_hip`, `vmaf_fex_float_moment_hip`,
  `vmaf_fex_integer_motion_v2_hip`, `vmaf_fex_float_motion_hip`,
  `vmaf_fex_float_ssim_hip`, `vmaf_fex_float_psnr_hip`,
  `vmaf_fex_float_adm_hip`, `vmaf_fex_cambi_hip`,
  `vmaf_fex_integer_vif_hip` remain unflagged because their kernels
  are still scaffold-only / -ENOSYS / crash on first dispatch.
  `vmaf_fex_integer_vif_hip` is cautionary tale: speculatively
  flagged in its batch-1 commit, but crashes with GPU memory access
  fault on first frame when dispatch picks it; ADR-0530
  un-flags it until kernel-level fix lands. **On rebase**: do NOT
  bulk-set flag on every HIP extractor. Promotion requires its own
  ADR + `vmaf --backend hip --feature <name>` reproducer showing HIP
  kernel launching (`AMD_LOG_LEVEL=3` shows
  `hipModuleLaunchKernel` trace) and VMAF score within places=4
  cross-backend gate of CPU twin. Three companion invariants pinned
  by ADR-0530 on dispatch side:
  (1) `compute_fex_flags()` in `libvmaf.c` adds
  `VMAF_FEATURE_EXTRACTOR_HIP` whenever `vmaf->hip.state` is set
  (host-pic only, like Vulkan — no gpumask gate);
  (2) `vmaf_get_feature_extractor_by_feature_name()` falls back to
  unflagged extractor when preferred-flag pass misses, so
  partially-ported HIP backend still routes missing features
  through CPU twins;
  (3) `flush_context_serial()` drains HIP-flagged extractors'
  `gpu_pending` final-frame collect (mirrors SYCL pattern in
  `flush_context_sycl`).
