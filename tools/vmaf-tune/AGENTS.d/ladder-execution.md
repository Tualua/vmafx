---
paths:
  - tools/vmaf-tune/src/vmaftune/ladder.py
  - tools/vmaf-tune/tests/test_ladder*.py
invariant: Reference decode scales to rung target; ladder --duration bounds encode and decode; RC=2 on operational failure.
---
<!-- markdownlint-disable MD024 -->
# Bitrate ladder execution and scaling

- **`_maybe_decode_reference` scales reference YUV to rung target
  on cross-resolution sweeps (ADR-0501, Bug #V4-B).** When
  `CorpusJob.src_width / src_height` differs from `width / height`,
  `iter_rows` passes rung target to `_maybe_decode_reference`
  which appends `-vf scale=W:H` to ffmpeg decode argv. It embeds
  dims in sidecar filename (`<src>.ref.decoded.<W>x<H>.yuv`) so
  multi-rung sweeps don't collide on stale path.
  Single-resolution rungs (src dims == rung dims, or both `None`)
  keep legacy native-geometry decode. Without this scale, libvmaf
  CLI silently mis-parses planar bytes (1080p reference handed to
  720p-reading CLI = ~21 VMAF instead of ~93) and collapses ladder
  grid.
- **`vmaf-tune-ladder/v1` JSON always emits `samples[]` array
  (ADR-0501, Bug #V4-B).** `emit_manifest(format="json", samples=…)`
  threads pre-hull sampler cloud through `build_and_emit`. Empty
  array when no cloud wired — never missing key — so consumers can
  read `payload["samples"]` unconditionally. HLS / DASH emitters
  silently ignore `samples=` kwarg.
- **Sample cloud is full per-CRF sweep, de-duplicated by
  `(width, height, crf)` (ADR-0505, Bug #V5-2 + #V5-3).** CLI's
  `_run_ladder` constructs local `cloud_sink: list[LadderPoint]`,
  passes it to `make_default_sampler(cloud_sink=…)`, and threads
  it into `build_and_emit(extra_samples=…)`. Sampler appends every
  successfully-scored CRF row from `iter_rows` into sink before
  `pick_target_vmaf` collapses cell. Emitted `samples[]` array
  carries every encoded CRF row per resolution instead of
  one-row-per-target-cell (V4 emit shape). Emit-side
  `_dedup_samples` pass keys on `(width, height, crf)` so two
  targets converging on same CRF emit one sample row, not two.
  `_run_ladder` test stubs that fabricate `LadderPoint` instances
  with same `(w, h, crf)` triple across different cells will
  collapse in JSON descriptor as designed.
- **`ladder --duration N` bounds encode pipe AND reference decode
  (ADR-0506, Bug #V6-1).** `EncodeRequest.duration_s` is plumbed
  by `iter_rows` from `CorpusJob.duration_s`;
  `build_ffmpeg_command` appends `-t duration_s` as input-side
  flag iff `sample_clip_seconds == 0.0 AND duration_s > 0`.
  Regression that drops `duration_s` field or skips fallback
  branch re-introduces "ladder smoke run takes 10 minutes per
  cell" bug. Encoder will process full source while only
  `duration_s` seconds of reference is decoded for scoring.
  Sample-clip mode (ADR-0297) keeps precedence because it carries
  centred start offset.
- **Raw-YUV reference decode emits demuxer-side flags before
  `-i` (ADR-0506, Bug #V6-2).** `_decode_source_to_yuv` requires
  `source_width` / `source_height` when `source_is_raw=True`
  (raises `ValueError` otherwise) and prepends `-f rawvideo
  -pix_fmt <pf> -s SRCWxSRCH -r FR` before `-i`. Container path
  (`source_is_raw=False`, default) keeps auto-detect argv
  unchanged so every v3/v4/v5 container test still passes.
  `_maybe_decode_reference` derives `source_is_raw` from source
  suffix and forwards `iter_rows`'s
  `job.src_width / src_height / framerate` (or, when those are
  `None`, rung dims as legacy single-res case). Regression that
  drops demuxer-side block when raw is shape re-introduces
  "default sampler produced no scorable encodes" on every
  cross-res rung against raw source.
- **`_run_ladder` returns RC=2 on operational failure
  (ADR-0506, Bug #V6-3).** `build_and_emit` can legitimately
  raise `RuntimeError` (no scorable encodes) or `ValueError`
  (bad input). Wrapper prints exception message to stderr and
  returns 2; do not widen exception list to bare `Exception`
  (that would swallow programmer errors and `KeyboardInterrupt`).
- **`vmaf-tune ladder --score-backend` resolves up-front;
  `tune-per-shot --score-backend` defers to libvmaf (ADR-0511).**
  `_run_ladder` calls
  `score_backend.select_backend(prefer=raw_backend, vmaf_bin=args.vmaf_bin)`
  BEFORE any encode starts so unavailable backend errors out
  with RC=2 and clear message instead of failing mid-sweep with
  cryptic libvmaf output. Resolved value threads through
  `make_default_sampler(score_backend=...)` →
  `CorpusOptions.score_backend` → `vmaf --backend $name`. When
  `auto` resolves to `cpu`, value passed downstream is `None` so
  corpus step omits explicit `--backend` flag and lets libvmaf
  use its own default (preserves legacy zero-flag invocation
  pattern). `_run_tune_per_shot` DELIBERATELY DOES NOT
  pre-resolve — `_build_per_shot_bisect_predicate` keeps
  historical
  `None if args.score_backend == "auto" else args.score_backend`
  conversion so `bisect_target_vmaf` receives `None` for auto and
  lets libvmaf pick live runtime at scoring time. Asymmetry is
  documented inline at top of `_run_tune_per_shot`; do not "fix"
  it without updating that contract and predicate tests.
- **`_ladder_point_from_row` returns union annotation hides
  (ADR-0888).** `tools/vmaf-tune/src/vmaftune/ladder.py` documents
  that `UncertaintyLadderPoint` is deliberately NOT subclass of
  `LadderPoint` (the "subclassing would require runtime isinstance
  gymnastics" comment). Function returns either type at runtime,
  depending on whether row carries `vmaf_interval`; downstream tests
  (`test_ladder.py::test_build_ladder_default_sampler_preserves_vmaf_interval`)
  assert runtime variant. Return annotation is kept as narrower
  `LadderPoint` with explicit `cast` because widening whole
  `Ladder.points` chain cascades through public ladder API
  (`build_ladder`, `make_default_sampler`, `select_knees`,
  `convex_hull`). If future change does promote union into public
  surface, lift `cast` and audit every `Ladder.points` consumer
  for `isinstance` discriminators.
