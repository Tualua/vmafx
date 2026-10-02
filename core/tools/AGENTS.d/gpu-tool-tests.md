---
paths:
  - core/tools/test/meson.build
invariant: Every test under core/tools/test/ carrying Meson gpu suite tag must set is_parallel : false.
---
# GPU-tagged tool tests run exclusively

Every test under `core/tools/test/` carrying Meson `gpu` suite tag must
also set `is_parallel : false`. These shell-driven CLI tests consume same
physical accelerator as kernel tests under `core/test/`; leaving either
`test_vmaf_cuda_gpumask` or `test_vmaf_<backend>_threads` registration
parallel defeats shared-device scheduling contract. global
`check_gpu_test_serialization` test reads Meson introspection across whole
project, so keep these registrations visible to it and do not replace
suite tag with local-only convention.
