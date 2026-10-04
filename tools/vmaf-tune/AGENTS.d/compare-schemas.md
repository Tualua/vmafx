---
paths:
  - tools/vmaf-tune/src/vmaftune/compare.py
  - tools/vmaf-tune/tests/test_compare.py
invariant: compare JSON supports v1 and v2 schemas; bisect_samples row field is optional and additive; COMPARE_ROW_KEYS stable.
---
<!-- markdownlint-disable MD024 -->
# Compare JSON schemas

- **`compare` JSON has two schemas in tree (v1 + v2)** — pick
  ingester by discriminator, never by row count. v1 (single-target
  legacy) has no `schema_version` key and no `target_vmafs` list,
  carries one row per codec at single `target_vmaf`. v2
  ([ADR-0516](../../../docs/adr/0516-vmaf-tune-compare-rate-quality-sweep.md))
  stamps `"schema_version": 2` and `"target_vmafs": [...]`, emits
  one row per `(codec, target_vmaf)` pair. Discriminator is
  `schema_version >= 2 OR "target_vmafs" in payload`; helper
  `vmaftune.compare.detect_schema_version()` is single source of
  truth and **must not be inlined** into renderers. Both shapes
  share per-row key set (`COMPARE_ROW_KEYS`) — adding columns to one
  schema means adding to both. When renderer encounters v2 payload
  it draws rate-quality curve + pareto-frontier overlay; v1 keeps
  legacy bar+dot chart. Operators that consume JSON programmatically
  can `if payload.get("schema_version", 1) >= 2:` branch on
  contract.
- **v2 schema's `bisect_samples` row field is optional and additive
  (ADR-0534).** Every successful encode+score round-trip underlying
  bisect computes is appended to `BisectResult.samples` and
  projected through `RecommendResult.bisect_samples` (tuple of
  dicts with `crf`, `bitrate_kbps`, `vmaf_score`, `encode_time_ms`).
  `to_row` emits field only when populated so absence of key still
  identifies "old v2 dump (pre-ADR-0534)" — renderer falls back to
  legacy connect-the-dots chart with caveat note in that case. Chart
  deduplicates samples per codec by CRF, sorts by bitrate, draws
  monotonic-friendly per-codec curve with picked-CRF rows
  highlighted as larger circled markers. **Do not collapse
  `bisect_samples` into "winner only" field** — that defeats whole
  purpose of additive plumb. CSV emitter intentionally drops
  structured column via `extrasaction="ignore"`; preserving flat row
  contract is load-bearing for downstream `csv` consumers (e.g.
  spreadsheet ingestion).
- **`COMPARE_ROW_KEYS` is JSON / CSV output contract** for
  `vmaf-tune compare`. Same maintenance discipline as
  `CORPUS_ROW_KEYS`: adding optional keys with default is fine,
  renaming or removing keys requires bumping schema and updating
  every downstream consumer in same PR.
