---
paths:
  - ai/scripts/materialize_saliency_features.py
  - ai/scripts/batch_materialize_saliency_features.py
  - ai/batch-manifests/saliency/*
invariant: Saliency materializer stays table-side; batch manifests orchestrate joins without duplicating inference.
---
<!-- markdownlint-disable MD013 MD060 -->
# Saliency feature materialization

- [ADR-0672](../../docs/adr/0672-saliency-materializer-temporal-controls.md) — **saliency materializer rows must be attributable.** `ai/scripts/materialize_saliency_features.py` exposes same temporal reducers as `vmaf-tune` (`mean`, `ema`, `max`, `motion-weighted`), records `saliency_model_id`, `saliency_aggregator`, and `saliency_ema_alpha` for newly materialized rows. Do not overwrite or invent provenance metadata for skipped pre-existing saliency columns; use `--overwrite` when intentionally replacing them.
- [ADR-0993](../../docs/adr/0993-konvid-ugc-bvi-saliency-batch-launch.md) — **KoNViD / UGC / BVI-DVC saliency batch manifests.** In-tree manifests live under `ai/batch-manifests/saliency/`. KoNViD-150K manifest (`konvid-150k.json`) fully wired to `konvid_150k.jsonl` with `path_column=src`, `root=.corpus/konvid-150k/k150ka_extracted/`. UGC (`ugc.json`) and BVI-DVC (`bvi-dvc.json`) = scaffolded stubs with `tables: []` until path-enriched corpus JSONL generated for each (see `_status` / `_resolution` comments in each manifest). Never populate UGC tables using `source` identifier column from full-feature parquet — contains corpus IDs, not file paths. Never populate BVI-DVC tables using `key` column — contains encode parameters, not file paths.

## Saliency feature materialization (ADR-0655)

`ai/scripts/materialize_saliency_features.py` = reusable bridge from
row-oriented corpus tables to saliency-bearing training rows. Keep saliency
inference out of trainer hot loops: trainers consume `saliency_mean` /
`saliency_var` columns and report missing coverage, while this script owns
bounded FFmpeg decode, ffprobe fallback, model invocation, and
`saliency_status` audit column. Do not add corpus-specific one-off saliency
materializers unless future ADR explains why shared table utility cannot
represent corpus.
`ai/scripts/batch_materialize_saliency_features.py` is only orchestration over
shared materializer: batch manifests may select tables, roots, model ids,
and temporal reducers, but must not duplicate row decoding, saliency inference,
or status semantics.
