---
paths:
  - core/src/feature/hip/hip_hsaco_stubs.c
  - core/src/feature/hip/integer_adm_hip.c
invariant: Remove weak HSACO stub symbols when real HIP kernels land.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# Remove the weak HSACO stub the moment a real .hip lands (ADR-0539)

`.hip` kernel under `feature/hip/<extractor>/` becoming
standalone-buildable, registered in `hip_kernel_sources` in
`core/src/meson.build` -> **also delete matching
`VMAF_HSACO_WEAK_STUB(<extractor>_score_hsaco)` line from
`hip_hsaco_stubs.c` in same PR.** Leaving stub creates two
definitions of same symbol — strong xxd-embedded blob and weak
1-byte fallback. Linker resolves to strong one, but at cost of
`-Wlto-type-mismatch` warnings on every build. User direction = "no
stubs anywhere" once real kernel exists.

Pattern (ADR-0539 example for `float_vif_score`):

1. Confirm `.hip` source compiles via `hipcc --genco` in container
   (`ninja -C <build> src/<name>.hsaco`).
2. Remove `VMAF_HSACO_WEAK_STUB(<name>_hsaco)` line from
   `hip_hsaco_stubs.c`. Leave one-line comment citing ADR so
   reviewer sees why slot is gone.
3. Rebuild with `enable_hipcc=true`, grep ninja output for warnings
   referencing symbol — none should remain.

## ADM `_hsaco` weak-stub slots have been removed (ADR-0539)

`hip_hsaco_stubs.c` weak fallbacks for `adm_dwt2_hsaco`,
`adm_csf_hsaco`, `adm_csf_den_hsaco`, `adm_cm_hsaco` have been
**removed** — four `.hip` kernels now build standalone via
`hipcc --genco` (registered in `core/src/meson.build::hip_kernel_sources`);
their xxd-embedded strong symbols supply blobs host TU loads.

Future ADM PR re-introducing CUDA-only helper into one of four
kernels (re-breaking standalone build) -> do NOT re-add weak stub —
fix kernel. Falling back to weak stubs silently degrades HIP ADM to
CPU at runtime (`hipModuleLoadData` call returns non-zero on empty
blob, extractor returns `-ENOSYS` from `init()`). User directive "no
stubs anywhere" explicitly rules this out.

`VMAF_HSACO_WEAK_STUB` macro in `hip_hsaco_stubs.c` retained as
documented pattern for in-progress ports of *new* extractors;
currently used by zero extractors.
