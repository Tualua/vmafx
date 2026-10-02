---
paths:
  - core/src/feature/hip/ciede_hip.c
  - core/src/feature/hip/integer_adm_hip.c
invariant: Do not re-add deleted orphan or dead translation units without consulting ADR-0546.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# Deleted orphan/dead TUs (ADR-0546)

Following files removed from this directory by ADR-0546
(`chore/hip-cuda-orphan-tu-cleanup`, 2026-05-18):

- `adm_hip.c` — defined `vmaf_hip_adm_{init,run,destroy}` stubs
  (`init` returned 0, `run` returned -ENOSYS); no
  `VmafFeatureExtractor` registration; zero callers in repo.
  API-level HIP ADM covered by `integer_adm_hip.c`
  (`vmaf_fex_integer_adm_hip`).
- `motion_hip.c` — same pattern; `vmaf_hip_motion_{init,run,destroy}`;
  covered by `integer_motion_hip.c` and `float_motion_hip.c`.
- `vif_hip.c` — same pattern; `vmaf_hip_vif_{init,run,destroy}`;
  covered by `integer_vif_hip.c` and `float_vif_hip.c`.
- `feature_hip.h` — forward-declared only above three triplets;
  removed with last of its consumers.

Also removed from `core/src/feature/hip/`:

- `adm_decouple.hip` (in `integer_adm/`) — dead uncompiled file
  removed by ADR-1154; decoupling already inlined in `adm_csf.hip`.
- `integer_moment_hip.h` and `integer_moment/moment_score.hip` —
  orphan header and duplicate kernel removed by ADR-1154; canonical
  implementation = `float_moment_hip.c` using
  `float_moment/moment_score.hip`.
- `integer_ciede_hip.c` — duplicate of `ciede_hip.c`; both defined
  `vmaf_fex_ciede_hip`. Only `ciede_hip.c` in `hip/meson.build`.
- `integer_moment_hip.c` — duplicate of `float_moment_hip.c`; both
  defined `vmaf_fex_float_moment_hip`. Only `float_moment_hip.c` in
  `hip/meson.build`.

And from `core/src/feature/cuda/`:

- `float_ssim_cuda.c` — stale copy superseded by
  `integer_ssim_cuda.c`; both defined `vmaf_fex_float_ssim_cuda`.
  Only `integer_ssim_cuda.c` in `core/src/meson.build`. Newer TU adds
  `enable_chroma` and other improvements missing from orphan copy.

Do not re-add any of these files without first consulting ADR-0546 /
ADR-1154.
