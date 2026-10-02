---
paths:
  - tools/vmaf-tune/src/vmaftune/auto.py
  - tools/vmaf-tune/src/vmaftune/recommend.py
  - tools/vmaf-tune/tests/test_auto*.py
  - tools/vmaf-tune/tests/test_recommend*.py
invariant: Auto emits one selected winner; 7 short-circuit predicates ordered; F.4 recipe overrides are read-only factories.
---
<!-- markdownlint-disable MD024 -->
# Auto-tuning policy and recipe selection

- **`auto` non-smoke source probing is real planning path.**
  `run_auto(smoke=False, meta_override=None)` must route source
  metadata through `_probe_source_meta`: ffprobe geometry, ffprobe
  duration, and `hdr.detect_hdr` share same subprocess runner seam.
  Keep failures conservative (1920x1080 SDR, `duration_s=0.0`) so
  planner can still emit auditable JSON plan instead of depending
  on host ffprobe quirks or reintroducing `NotImplementedError`.
- **`auto` emits one selected winner.** `run_auto` must keep
  `metadata.winner` aligned with single `cells[].selected == true`
  row whenever winner status has `cell_index`; evidence-failure
  plans may report `no_eligible_cells` with no selected row.
  Selector is quality/budget ordered per ADR-0428: first in-budget
  target passes, then target passes with smallest budget overage,
  then closest quality miss. Do not make callers infer winner from
  cell order.
- **`recommend` is pure consumer of corpus schema.** `recommend`
  subcommand reads `vmaf_score`, `bitrate_kbps`, `crf`, `preset`,
  `encoder`, `exit_status` directly from rows produced by
  `corpus.py` (or loaded via `--from-corpus` from previous run). No
  new schema, no parallel data path. If `SCHEMA_VERSION` bumps,
  `recommend.py`'s row-reader is one of downstream consumers that
  must be updated in same PR — contract is checked by
  `test_recommend.py` against `CORPUS_ROW_KEYS`.
- **Predicate semantics are part of user-visible contract.**
  `--target-vmaf T` returns *smallest CRF* whose `vmaf_score >= T`
  (falling back to closest-miss when nothing clears, marked
  `(UNMET)`). `--target-bitrate KBPS` returns row with minimum
  `|bitrate_kbps - KBPS|`, ties broken by smaller CRF. Two flags
  are mutually exclusive at argparse layer (exit code 2 when both
  passed). Changing any of these defaults is user-visible
  behaviour change requiring ADR.
- **Phase F 2-pass goes through adapter, not driver (ADR-0333).**
  Codecs opting into 2-pass encoding declare
  `supports_two_pass = True` and override
  `two_pass_args(pass_number, stats_path) -> tuple[str, ...]` on
  their adapter (today: `X264Adapter`, returning
  `('-pass', str(N), '-passlogfile', str(path))`, and
  `X265Adapter`, returning
  `('-x265-params', f'pass={N}:stats={path}')`). Encode driver
  (`encode.py`) calls adapter via
  `getattr(adapter, "supports_two_pass", False)`
  `adapter.two_pass_args(...)` — never branches on codec name.
  `EncodeRequest` carries `pass_number: int = 0` (0 = single-pass /
  default; 1 / 2 = pass index) and `stats_path: Path | None = None`.
  `build_ffmpeg_command` redirects pass-1 output to `-f null -` so
  throwaway encoded bitstream isn't written. 2-pass loop itself
  lives in `run_two_pass_encode` in `encode.py`; materialises
  stats file in `tempfile.mkdtemp` (or caller-supplied
  `scratch_dir`) and removes it (plus known encoder sidecars such
  as libx265's `.cutree`) on exit. When
  `supports_two_pass = False`, driver falls back to single-pass
  with stderr warning by default (`on_unsupported="fallback"`), or
  raises with `on_unsupported="raise"` — matches saliency.py
  "unsupported ROI encoder, fallback to plain encode" precedent.
  Sibling codec adapters (libsvtav1, libvvenc, libaom-av1) inherit
  this seam without touching driver — their PRs only need to
  override `supports_two_pass` + `two_pass_args` on adapter file.
  NVENC's `-multipass` is **not** this seam (single-invocation
  lookahead, not stats-file two-call sequence); separate adapter
  contract is follow-up if demand surfaces.
- **`auto` records HDR args through same dispatch table.**
  `run_auto` must call `hdr_codec_args(codec, info)` per cell when
  `meta.is_hdr` is true. Generic tuple such as
  `("-color_primaries", "bt2020", "-color_trc", "smpte2084")`
  is insufficient because x265, SVT-AV1, HEVC hardware encoders,
  AV1 hardware encoders, and VVenC use different ffmpeg flag
  families. Hardware HEVC rows force `p010le` + `main10`; hardware
  AV1 rows force `p010le`; codec-private SEI flags stay limited to
  families with stable FFmpeg knobs. Tests in
  `tests/test_auto_short_circuits.py` lock this per-codec shape.

- **Seven F.2 short-circuit predicates in ``auto.py`` are ordered
  tuple, not set.** ``SHORT_CIRCUIT_PREDICATES`` declares
  ``ShortCircuit.LADDER_SINGLE_RUNG`` first and
  ``ShortCircuit.SKIP_PER_SHOT`` last; order is part of public
  contract because tests assert determinism across
  `evaluate_short_circuits` invocations and JSON schema records
  canonical-order list under ``plan.metadata.short_circuits``.
  Adding eighth short-circuit (F.3+ follow-ups) appends to tuple;
  never insert in middle. Phase D thresholds
  (`PHASE_D_DURATION_GATE_S = 300.0` and
  `PHASE_D_SHOT_VARIANCE_GATE = 0.15`) are placeholders pending
  F.3 empirical fit — change them via ADR-0325 follow-up, not
  drive-by tweak. See
  [ADR-0325](../../../docs/adr/0325-vmaf-tune-phase-f-auto.md).
- **F.3 confidence-aware thresholds are corpus-derived; do not
  hand-pick.** `DEFAULT_TIGHT_INTERVAL_MAX_WIDTH = 2.0` and
  `DEFAULT_WIDE_INTERVAL_MIN_WIDTH = 5.0` in `auto.py` are
  emergency floor (Research-0067), not target. Production
  thresholds load from calibration JSON sidecar emitted by
  conformal-VQA pipeline (ADR-0279) — keys
  `tight_interval_max_width` and `wide_interval_min_width`.
  `load_confidence_thresholds` falls back to defaults with
  one-line WARNING when no sidecar found; do not silence that
  warning, and do not "tune" defaults to make failing integration
  test pass. Fix for surprising cell escalations on real data is
  recalibration PR, not threshold loosening here (CLAUDE.md
  `feedback_no_test_weakening`). Decision helper
  `_confidence_aware_escalation` is pure function of
  `(verdict, interval_width, thresholds)` so it stays trivially
  unit-testable; keep it pure when extending decision table.
  `run_auto` must pass recipe-adjusted `effective_thresholds` from
  `_apply_recipe_override` into every F.3 decision and into
  `plan.metadata.confidence_thresholds`; computing adjusted value
  and then falling back to `ConfidenceThresholds()` is user-visible
  planning bug.
- **F.4 recipe overrides are read-only factories, not literal
  dicts.** `_CONTENT_RECIPE_TABLE` in `auto.py` stores
  **callables** (`_animation_recipe`, `_screen_content_recipe`,
  `_live_action_hdr_recipe`, `_ugc_recipe`, `_empty_recipe`);
  every call returns fresh dict so caller mutating return value
  cannot leak mutation into next `run_auto` invocation. Tests in
  `tests/test_auto_recipe_overrides.py` assert this invariant
  explicitly. Adding new content class means adding factory
  function and `RECIPE_CLASS_<NAME>` constant; never inline
  literal dict into table or mutate one in place. Four override
  keys (`tight_interval_max_width`, `force_single_rung`,
  `saliency_intensity`, `target_vmaf_offset`) are only keys
  driver honours — `get_recipe_for_class` filters by
  `_RECIPE_KEYS` allowlist as defence-in-depth. Every threshold
  value shipped at F.4 is
  `[provisional, calibrate against real corpus in F.5]`; do not
  promote placeholder to "calibrated" in drive-by edit. Per
  memory `feedback_no_test_weakening`, `target_vmaf_offset` shifts
  only predictor's effective target; input `--target-vmaf` (gate
  that ships models) is preserved verbatim in
  `plan.metadata.target_vmaf`. See
  [ADR-0325](../../../docs/adr/0325-vmaf-tune-phase-f-auto.md) §F.4.
