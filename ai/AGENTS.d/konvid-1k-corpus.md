---
paths:
  - ai/scripts/konvid_to_full_features.py
  - ai/scripts/konvid_to_vmaf_pairs.py
  - ai/scripts/combine_full_feature_parquets.py
invariant: KoNViD-1k refresh requires fresh fork binary; stable balanced hash fold assignment; replay manifest.
---
<!-- markdownlint-disable MD013 MD060 -->
# KoNViD-1k full-feature refresh

`ai/scripts/konvid_to_full_features.py` = regeneration path for
`runs/full_features_konvid.parquet` and
`runs/full_features_konvid_with_folds.parquet`. Mirrors
`konvid_to_vmaf_pairs.py`'s synthetic-FR recipe (source MP4 as
reference, libx264 CRF 35 distorted side) but emits current
`FULL_FEATURES` tuple plus `vmaf` from fork CPU binary.

**Rebase-sensitive invariants:**

- Use `core/build-cpu/tools/vmaf` or explicitly verified fresh
  dev-container binary. Do not let script fall back to
  `/usr/local/bin/vmaf`; system installs have previously lacked
  fork-only extractors such as `motion_v2`, `ssimulacra2`, and   SpEED features.
- Folded parquet's `source=fold0..fold4` assignment = stable
  balanced hash over clip keys. Intentionally does not depend on
  directory enumeration order or row count, because
  `eval_multiseed_v3_v4.py` treats `source` as held-out fold key.
- Default cache path includes `FULL_FEATURES` count and CRF.
  Feature tuple or distortion recipe changing -> write new cache
  namespace rather than reusing stale per-clip libvmaf JSON.
- Aggregate `runs/full_features_*corpus*.parquet` files rebuilt via
  `ai/scripts/combine_full_feature_parquets.py`, not ad hoc notebook
  concatenation. Output schema =
  `corpus, source, frame_index, codec, <FULL_FEATURES>, vmaf`.
- `konvid_to_full_features.py` writes
  `runs/full_features_konvid.manifest.json` by default with KoNViD
  root/cache/model inputs, fold settings, selected/processed clip counts,
  output paths, and ADR-0661 `run_provenance`; keep it with refreshed
  parquet pair.
