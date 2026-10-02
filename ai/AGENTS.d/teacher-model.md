---
paths:
  - ai/data/scores.py
  - ai/data/feature_extractor.py
  - ai/scripts/combine_full_feature_parquets.py
invariant: Teacher model follows single source resolve_teacher_model; stamped on every row; mixed teachers refused.
---
<!-- markdownlint-disable MD013 MD060 -->
# AI teacher model resolution and table provenance

- [ADR-1173](../../docs/adr/1173-ai-teacher-follows-default-model.md) — **AI teacher model follows default model single source.** AI training and extraction scripts resolve teacher model through `ai.data.scores.resolve_teacher_model()` (imports `DEFAULT_MODEL` from `vmaftune.defaultmodel`), falling back to `$VMAF_MODEL_PATH` then `DEFAULT_MODEL`. Feature producers stamp `teacher_model` on every row and manifest; combiners and trainers refuse mixed-teacher tables without `--assume-teacher`; raw feature extraction tables append `adm3` to `FULL_FEATURES` and K150K `FEATURE_NAMES` while canonical-6 student features remain frozen.

- **AI teacher model single source and table provenance invariants (ADR-1173).**
  (1) AI training and extraction scripts resolve their teacher model through
  `ai.data.scores.resolve_teacher_model()` (which imports `DEFAULT_MODEL` from
  `vmaftune.defaultmodel`), falling back to `$VMAF_MODEL_PATH` then `DEFAULT_MODEL`.
  No script under `ai/` may hardcode `"vmaf_v0.6.1"` or any literal model fallback.
  (2) Feature producers (`extract_full_features.py`, `extract_k150k_features.py`,
  `bvi_dvc_to_full_features.py`, `extract_ugc_features.py`, `konvid_to_full_features.py`,
  `konvid_to_vmaf_pairs.py`, `bvi_dvc_to_corpus_jsonl.py`) unconditionally write   `teacher_model` column on every row.
  (3) Combiners and trainers (`combine_full_feature_parquets.py`,
  `train_vmaf_tiny_v5.py`, `eval_loso_vmaf_tiny_v5.py`) verify teacher uniformity within
  and across all input shards. Shards with different teacher models are strictly refused.
  Tables lacking `teacher_model` column are rejected unless `--assume-teacher <name>`
  is explicitly passed for legacy datasets.
  (4) Raw extraction feature lists (`FULL_FEATURES` in `ai/data/feature_extractor.py` and
  `FEATURE_NAMES` in `ai/scripts/extract_k150k_features.py`) include `"adm3"`.   canonical-6 student feature set (`DEFAULT_FEATURES`: `adm2`, `vif_scale0..3`, `motion2`)
  remains strictly frozen.
