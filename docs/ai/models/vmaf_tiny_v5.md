# `vmaf_tiny_v5` — deferred model card

> **Status**: Deferred. No `vmaf_tiny_v5.onnx` is shipped.

Per [ADR-0287](../adr/0287-vmaf-tiny-v5-corpus-expansion.md) the
`vmaf_tiny_v5` corpus-expansion experiment was **deferred** — the
training corpus expansion produced a smaller PLCC delta than the
v3 → v4 step, and the architecture ladder is treated as saturated
at v4 (per
[Research-0048](../research/0048-vmaf-tiny-v4-mlp-large-evaluation.md)).
There is no `vmaf_tiny_v5.onnx` shipped in `model/tiny/` and no
`v5` runtime path. This page exists so a reader looking for a v5
model card finds the deferral verdict instead of a missing document.

## Operator Impact

- Do not reference `vmaf_tiny_v5` in `model/tiny/registry.json`.
- Use [`vmaf_tiny_v4`](vmaf_tiny_v4.md) when you need the top shipped
  tiny-VMAF architecture.
- Treat any proposed v5 work as a new model-development PR with fresh
  training data, metrics, model card updates, and ADR coverage.
- Refresh the UGC side with `ai/scripts/extract_ugc_features.py`
  before any new v5 experiment. The extractor now emits the current
  `FULL_FEATURES` schema instead of the historical canonical-6-only
  table, while the v5 trainer still selects only canonical-6 + `vmaf`.
- Any exploratory `ai/scripts/eval_loso_vmaf_tiny_v5.py` report includes
  `run_provenance` for both the base and extra parquet inputs plus the report
  target. Use that block to compare corpus-expansion experiments; it does not
  change the current deferred ship verdict.
- Any exploratory `ai/scripts/train_vmaf_tiny_v5.py --out-stats` JSON also
  includes `run_provenance` for the base/extra parquet inputs, checkpoint
  target, stats target, argv, and hyperparameters. Keep that block with local
  experiments so rejected v5 refreshes remain auditable.

## See also

- [`vmaf_tiny_v4.md`](vmaf_tiny_v4.md) — the current top-of-ladder
  tiny model (architecture ladder ends here per the v4 evaluation).
- [`vmaf_tiny_v3.md`](vmaf_tiny_v3.md) — the second-from-top tiny
  model (also shipped).
- [ADR-0287](../adr/0287-vmaf-tiny-v5-corpus-expansion.md) —
  defer-decision context.
