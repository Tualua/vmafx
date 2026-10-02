---
paths:
  - tools/vmaf-tune/src/vmaftune/per_shot.py
  - tools/vmaf-tune/tests/test_per_shot.py
  - tools/vmaf-tune/tests/test_tune_per_shot_container_src.py
invariant: vmaf-perShot is canonical detector; scene-threshold + uniform-window splitter; segment-dir priority order.
---
<!-- markdownlint-disable MD024 -->
# Per-shot tuning and scene detection

- **CLI default is real per-shot bisect.**
  `vmaf-tune tune-per-shot` must call Phase-B bisect backend
  unless `--predicate-module MODULE:CALLABLE` is explicitly
  supplied. Do not reintroduce adapter-default CRF as CLI
  behaviour; that fallback exists only for library dry runs that
  call `tune_per_shot()` without predicate.
- **Shot ranges are half-open inside Python.** C-side
  ``vmaf-perShot`` JSON/CSV sidecar uses inclusive ``end_frame``;
  ``per_shot.py`` normalises into ``[start_frame, end_frame)`` at
  parse boundary. ``Shot.length`` and ``-frames:v`` arg in
  ``_segment_command`` both depend on half-open form. Do not
  "round-trip back to inclusive" — every downstream consumer
  assumes half-open form.
- **``vmaf-perShot`` binary surface is canonical detector.** Do not
  add parallel ONNX-Runtime-from-Python detector path. When
  TransNet V2 is hot-pathed (e.g. Phase E ladder generation
  re-running detection), extend ``detect_shots`` to call
  ``vmaf-perShot`` once and cache, not to bypass binary.
- **Scene-threshold + uniform-window splitter (ADR-0512).**
  ``detect_shots`` accepts ``diff_threshold`` (forwarded to C
  binary as ``--diff-threshold``) and ``max_shot_duration_sec``
  (post-processing splitter, requires ``framerate``). CLI exposes
  these as ``--scene-threshold`` and ``--max-shot-duration``
  (default ``2.0 s``, ``0`` disables). Splitter is intentionally
  default-on so 5 s clips always produce ``>= 2`` shots even when
  luma-delta heuristic under-cuts; lowering default is behavioural
  change that must come with fresh empirical calibration against
  BBB e2e fixtures. ``split_long_shots`` helper preserves
  contiguity (``out[i].end_frame == out[i+1].start_frame``) and
  distributes remainder so partition lengths differ by at most
  one frame — both invariants are covered by ``test_per_shot.py``
  and downstream merge / concat-listing code depends on
  contiguity property.
- **Segment-dir priority order is load-bearing (ADR-0532).** CLI
- **Segment-dir priority order is load-bearing (ADR-0530).** CLI
  resolves concat-listing directory in this exact order: (1)
  ``--segment-dir`` when set; (2) ``plan_out.parent / "segments"``
  when ``--plan-out`` is set; (3) ``output.parent / "segments"``
  otherwise. Order (2) ensures concat listing lands alongside
  plan JSON on writable path — plan write already succeeded at
  that point, so parent is guaranteed writable.
  ``write_concat_listing`` call is wrapped in ``OSError`` catch;
  failure emits ``WARN`` to stderr and command exits 0 (plan JSON
  is authoritative deliverable). Do not collapse orders (2) and
  (3) without updating this invariant and
  ``test_per_shot.py::test_cli_tune_per_shot_readonly_cwd_returns_zero``.
- **Shot detection runs once per source, never per cell.** Corpus
  driver (``corpus._resolve_shot_metadata``) calls
  ``_detect_shots_with_status`` at top of ``iter_rows`` and
  passes resulting ``ShotMetadata`` down to every
  ``(preset, crf)`` row via ``_row_for``. Moving call inside cell
  loop roughly doubles corpus wall time on TransNet-V2.
  ``_detect_shots_with_status`` is only API that returns
  ``(shots, ok)`` tuple needed to distinguish real single-shot
  source from "binary failed" fallback — public ``detect_shots``
  shape cannot carry that flag.

- **`_build_per_shot_bisect_predicate` returns 2-tuple
  (ADR-0531).** Function now returns
  `(predicate_fn, bitrate_sidecar)` where `bitrate_sidecar` is
  `dict[tuple[int, int], float]` keyed by
  `(start_frame, end_frame)`. Predicate closure populates dict as
  each shot's bisect completes; `_run_tune_per_shot` annotates
  each `ShotRecommendation` via
  `dataclasses.replace(r, bitrate_kbps=...)`. Custom
  `--predicate-module` callers are unaffected (sidecar is empty
  for that path). Do not change function signature back to bare
  `PerShotPredicateFn` return without also wiring alternative
  bitrate capture path. Plan JSON schema now requires
  `bitrate_kbps` per shot, and its absence causes report
  renderer to show "—".
- **`ShotRecommendation.bitrate_kbps` defaults to NaN (ADR-0531).**
  Field carries measured segment bitrate from bisect predicate.
  NaN is correct for dry-run / synthetic predicates that never
  encode real segment. Plan-JSON emitter serialises NaN as `null`
  (RFC-8259-portable); report ingester treats `null` and absent as
  NaN and renders "—". Tests that construct `ShotRecommendation`
  directly need not set `bitrate_kbps` unless testing bitrate
  column.
- **`tune-per-shot` geometry auto-probe: patch `args` in-place
  before any downstream call (ADR-0542).** `_run_tune_per_shot`
  writes ffprobe-derived width, height, framerate, and
  total-frames back onto `args` namespace at top of function.
  This lets `_build_per_shot_bisect_predicate`, `merge_shots`,
  plan serialisation, and `detect_shots` call all receive
  consistent geometry without signature changes. Branch condition
  is
  `not _source_needs_rawvideo_demux(args.src)` — raw YUV (`.yuv` /
  `.raw`) sources still require explicit `--width` and `--height`
  and exit 2 if omitted. Do not add new geometry-consuming helper
  inside `_run_tune_per_shot` without reading `args.width` /
  `args.height` AFTER probe block, not before. Tests:
  `tests/test_tune_per_shot_container_src.py`.
