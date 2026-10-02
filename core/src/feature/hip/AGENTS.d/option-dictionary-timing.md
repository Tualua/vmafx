---
paths:
  - core/src/feature/hip/integer_adm_hip.c
  - core/src/feature/hip/integer_ssim_hip.c
invariant: Serialize option dictionaries prior to extractor initialization and dispatch.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# Option dictionary serialization timing (ADR-1154)

Extractors providing features with parameterized names must call
`vmaf_feature_name_dict_from_provided_features` **before** assigning
internal dimension defaults (`s->w = w`, `s->h = h`) to options
marked with `VMAF_OPT_FLAG_FEATURE_PARAM`. Overwriting struct fields
with non-zero defaults before creating dictionary causes
`feature_name` to serialize dimensions as option overrides (e.g.
`_full_w_576_full_h_324`), which breaks feature lookups and parity
tests.
