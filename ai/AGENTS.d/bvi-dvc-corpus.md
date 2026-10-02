---
paths:
  - ai/scripts/bvi_dvc_to_full_features.py
  - ai/scripts/bvi_dvc_to_corpus_jsonl.py
  - ai/scripts/merge_corpora.py
invariant: BVI-DVC corpus is research-only; raw media never committed; dedup by (src_sha256, encoder, preset, crf).
---
<!-- markdownlint-disable MD013 MD060 -->
# BVI-DVC corpus ingestion (ADR-0310)

Bristol VI Lab BVI-DVC reference corpus = second training
shard for `fr_regressor_v2` alongside Netflix Public drop.
Pipeline: `bvi_dvc_to_full_features.py` (parquet + cached libvmaf
JSON) → `bvi_dvc_to_corpus_jsonl.py` (vmaf-tune `CORPUS_ROW_KEYS`
rows) → `merge_corpora.py` (concatenate with Netflix shard, dedup
by `(src_sha256, encoder, preset, crf)`).

**Rebase-sensitive invariants:**

- BVI-DVC is research-only. Archive
  (`.corpus/bvi-dvc-raw/BVI-DVC Part 1.zip`), extracted MP4s
  (`.corpus/bvi-dvc-extracted/`),
  feature parquet (`runs/full_features_bvi_dvc_*.parquet`), JSONL
  corpus shard (`runs/bvi_dvc_corpus.jsonl`), and cached vmaf JSON
  (`~/.cache/vmaf-tiny-ai-bvi-dvc-full/`) **never committed**. Fork
  redistributes derived `fr_regressor_v2_*.onnx` weights only —
  corpus-must-be-license-compatible-or-stay-local applies uniformly
  across `ai/` corpora (Netflix Public, BVI-DVC, KoNViD,
  YouTube-UGC).
- Merge contract = `vmaftune.CORPUS_ROW_KEYS` from
  `tools/vmaf-tune/src/vmaftune/__init__.py`. Bumping
  `SCHEMA_VERSION` means re-running BVI-DVC adapter to backfill
  new fields. Merge utility refuses any row missing required key —
  fail-loud by design.
- Natural-key tuple `(src_sha256, encoder, preset, crf)` = dedup
  contract. Re-encodes of same source under new `(preset, crf)`
  legitimately appear as distinct rows; do not fold them by
  `src_sha256` alone.
- Production-weights flip stays gated on
  [ADR-0303](../../docs/adr/0303-fr-regressor-v2-ensemble-flip.md).
  Adding BVI-DVC to corpus does NOT authorise re-shipping
  `fr_regressor_v2.onnx` without re-running ensemble gate.
- `bvi_dvc_to_full_features.py` writes
  `runs/full_features_bvi_dvc_<tier>.manifest.json` by default with
  input mode, tier, cache/model inputs, feature order, row/clip counts,
  and ADR-0661 `run_provenance`; keep manifest beside any refreshed
  local parquet.
- `bvi_dvc_to_full_features.py` accepts two mutually exclusive input
  modes: `--bvi-zip` (original; streams MP4s from archive) and
  `--bvi-dir` (ADR-0527; enumerates pre-extracted `.mp4` / `.yuv` files
  from directory). Tier inferred from resolution via closed
  `_RES_TO_TIER` dict — any new BVI-DVC release with   non-standard resolution requires adding row there before file
  gets picked up. Dir-mode path does **not** delete source files
  after processing; zip-mode path deletes temporarily extracted
  MP4 after processing (existing behaviour).
