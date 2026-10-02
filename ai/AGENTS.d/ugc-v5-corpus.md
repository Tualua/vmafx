---
paths:
  - ai/scripts/fetch_youtube_ugc_subset.py
  - ai/scripts/extract_ugc_features.py
  - ai/scripts/train_vmaf_tiny_v5.py
  - ai/scripts/eval_loso_vmaf_tiny_v5.py
invariant: v5 corpus expansion is research-only; no vmaf_tiny_v5 ships; raw videos never committed.
---
<!-- markdownlint-disable MD013 MD060 -->
# v5 corpus-expansion probe — research-only (ADR-0287)

`*_vmaf_tiny_v5.py` scripts
(`fetch_youtube_ugc_subset.py`, `extract_ugc_features.py`,
`train_vmaf_tiny_v5.py`, `eval_loso_vmaf_tiny_v5.py`) = research
infrastructure for deferred v5 corpus-expansion probe. **No
`vmaf_tiny_v5.onnx` ships** — 1-σ ship gate did not clear (Δ PLCC =
+0.00005 at seed=0, far below 1-σ_v2 threshold). Extending these
scripts:

- Do not add `vmaf_tiny_v5` row to
  `model/tiny/registry.json` unless follow-up run clears ship gate
  documented in
  [ADR-0287](../../docs/adr/0287-vmaf-tiny-v5-corpus-expansion.md).
- Fetcher hits public GCS bucket (`gs://ugc-dataset/`,
  CC-BY); raw videos and resulting
  `runs/full_features_ugc.parquet` must NEVER be committed
  (`runs/` and `.corpus/` trees are gitignored).
- `extract_ugc_features.py` emits same current `FULL_FEATURES`
  schema as other full-feature refresh scripts. Older versions
  intentionally populated only canonical-6, forced rest to NaN;
  do not restore that shortcut when refreshing `full_features_5corpus`.
- Dual-arm LOSO trains 18 mlp_small models
  (9 v2-baseline plus 9 v5-candidate); single invocation
  wall-time ~10–25 min depending on CPU. Do NOT launch it
  concurrently with another training process — two share
  BLAS threads and serialise badly.
