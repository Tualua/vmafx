---
paths:
  - core/src/meson.build
  - core/meson.build
invariant: Wire new HIP extractors into Meson source lists and library declarations cleanly.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# Wiring a new HIP extractor into the build (ADR-0852 lesson)

Three files must be updated together — omitting any one silently
leaves extractor unreachable:

1. **`core/src/meson.build` `hip_kernel_sources` dict** — add
   `'<kernel_name>'` entry pointing to `.hip` source so `hipcc
   --genco` compiles HSACO blob.
2. **`core/src/hip/meson.build` `hip_sources`** — add host `.c`
   wrapper so it's compiled into HIP runtime archive.
3. **`core/src/feature/feature_extractor.c`** — add
   `extern VmafFeatureExtractor` declaration inside `#if HAVE_HIP` and
   `&vmaf_fex_*_hip` pointer in dispatch table, so
   `vmaf_get_feature_extractor_by_name` can resolve it.

Failure mode (ADR-0852): `speed_chroma_hip` and `speed_temporal_hip`
(ADR-0567) had all three implementation files committed but missing
all three wiring entries, making extractors completely unreachable
for six weeks until ADR-0852 closed gap. CI matrix had no
`enable_hipcc=true` + `name-resolve` smoke test, so omission was
invisible until manual audit.
