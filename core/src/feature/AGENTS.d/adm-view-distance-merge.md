---
paths:
  - core/src/feature/integer_adm.c
  - core/src/feature/feature_extractor.h
  - core/src/fex_ctx_vector.cpp
  - core/src/rust/shim/rust_twins.cpp
  - core/src/rust/feature/adm/**
invariant: Second ADM distance = one context; transform once per scale, weigh per distance; scores equal separate contexts.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# ADM second viewing distance (ADR-2795)

- **Split per scale.** `integer_adm_scale0_transform()` /
  `integer_adm_scale_s123_transform()` = DWT + decouple, once per scale;
  `*_weigh()` = denominator, CSF, contrast masking, once per distance
  (`integer_adm_weigh_views()`). Weigh stages write only `csf_a` / `csf_f`, read
  DWT + decouple bands. Kernel change writing decouple or DWT band from weigh
  stage breaks every second distance; `test_integer_adm_view_dist` (`==`,
  8/10-bit, scalar + SIMD) catches.
- **Sums per distance in single-distance order.** `sums[v]` adds scale
  results in scale order, as one extractor alone; never fold distances.
- **One dictionary.** Second distance's scores use keys `<base>:nvde`
  (`VMAF_ADM_EXTRA_VIEW_KEY_SUFFIX`, `adm_extra_view_keys`). Only
  `adm_extend_name_dict()` maps keys, through `extend_name_dict` hook; C
  `init()` and Rust twin shim (`twin_init()`) both call hook. Rust
  `score.rs::EXTRA_VIEW_NAMES` repeats keys;
  `test_adm_view_dist_contract.py` pins both. Hook refuses second distance
  whose names equal first's.
- **Merge rules (`adm_merge_view_dist()`).** Compare feature names with
  distance neutralised, plus `adm_skip_aim`; decline incoming `debug`
  (unsuffixed debug scores vanish on merge; upstream loses those) or incoming
  second distance; absorb incoming at existing's second distance (upstream
  registers duplicate apart, collector refuses duplicate names). Option without
  `VMAF_OPT_FLAG_FEATURE_PARAM` added to `adm` table = extend merge checks;
  contract test lists handled set.
- **Registry (`offer_merge()`).** After dedup only; same extractor name and
  callback; never into initialized context. `adm` and `adm_rust` share
  callback (`add_twin()` copies descriptor); name check keeps both apart.
- **Twins.** CUDA, SYCL, HIP, Metal `adm` twins lack option until own PR
  (Q-298 stack); mirror tests record gap that fails once closed. Remove gap
  in same PR that adds option.
