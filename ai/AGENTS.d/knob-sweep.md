---
paths:
  - ai/scripts/analyze_knob_sweep.py
  - ai/tests/test_knob_sweep_analysis.py
invariant: Pareto frontiers per (source, codec, rc_mode); structural regressions (>=7 of 9 sources) must not ship.
---
<!-- markdownlint-disable MD013 MD060 -->
# Encoder knob-sweep analysis and regression policy

- [ADR-0305](../../docs/adr/0305-encoder-knob-space-pareto-analysis.md) — **knob-sweep corpus invariant.** 12,636-cell sweep at `runs/phase_a/full_grid/comprehensive.jsonl` (gitignored, locally generated) = source of truth for `tools/vmaf-tune/codec_adapters/*` recipe defaults. Pareto frontiers stratified per `(source, codec, rc_mode)` slice — never collapsed to global hull (companion [Research-0063](../../docs/research/0063-encoder-knob-space-cq-vs-vbr-stratification.md) shows global-hull failure mode regresses NVENC h264/hevc by ~4 VMAF at cq=30). **Recipes regressing vs bare encoder at matched bitrate within same slice MUST NOT ship as adapter defaults.** Regression-detection check lives in `ai/scripts/analyze_knob_sweep.py` (`detect_recipe_regressions(...)`), exercised by `ai/tests/test_knob_sweep_analysis.py::test_recipe_regression_detection`; new codec adapter PRs cite per-(codec, rc_mode) hull row from `reports/summary.md` (or "no hull entry yet — bare default") in PR description. Methodology + scaffolded findings: [Research-0077](../../docs/research/0077-encoder-knob-space-pareto-frontiers.md).

## Knob-sweep recipe-regression policy (ADR-0308)

Cited from regression-detection invariant in
[ADR-0305](../../docs/adr/0305-encoder-knob-space-pareto-analysis.md)
and policy decision in
[ADR-0308](../../docs/adr/0308-encoder-knob-sweep-recipe-regression-policy.md);
populated findings are in
[Research-0080](../../docs/research/0080-encoder-knob-sweep-findings.md).
Extending `ai/scripts/analyze_knob_sweep.py` or anything that
consumes its output:

- Recipe regression is *structural* iff it reproduces on **≥7 of
  9** corpus sources within single
  `(codec, rc_mode, recipe, preset, q)` cell. Structural regressions
  are forbidden as `tools/vmaf-tune/codec_adapters/*` defaults and
  forbidden as `vmaf-tune recommend` outputs without explicit
  override. Known-structural cells are listed in
  Research-0080 §Aggregated-bad-recipe-patterns; do not promote any
  of them to adapter-level default in follow-up PR.
- Recipe regression that hits 1-6 sources is *content-dependent*
  and is filtered at recommend-time via per-slice hull lookup,
  not at adapter-default time.
- Do NOT modify `ai/scripts/analyze_knob_sweep.py` to relax
  `bitrate_tol_pct` (default 5.0) or `vmaf_tol` (default 0.1)
  without ADR. Tolerances calibrated against per-frame VMAF
  noise floor and bitrate quantisation in libavformat muxers;
  loosening them silently masks structural cluster (see ADR-0305
  §Consequences).
- Detector is **offline** gate (3-hour sweep, ~2 GiB JSONL,
  single-host variance); do not wire it into CI without first
  designing smaller stratified sample that reproduces
  structural patterns. Tracked as follow-up in ADR-0308 §Decision
  point 4.
- Corpus producer (`hw_encoder_corpus.py`) currently emits
  `(src, actual_kbps, vmaf, enc_ms, recipe)` while
  `analyze_knob_sweep.SweepRow` consumes
  `(source, bitrate_kbps, vmaf_score, encode_time_ms,
  is_bare_default)`. Producer-side rename not yet landed
  (SCHEMA_VERSION=3 follow-up per ADR-0308 §Decision point 5); any
  analysis run goes through throw-away wrapper that performs
  rename in-process. Do NOT modify
  `analyze_knob_sweep.py` to accept both spellings.
