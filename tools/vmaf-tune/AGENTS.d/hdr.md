---
paths:
  - tools/vmaf-tune/src/vmaftune/hdr.py
  - tools/vmaf-tune/tests/test_hdr.py
invariant: HDR detection fails safe to SDR; select_hdr_vmaf_model resolves per source once; HDR codec dispatch table.
---
<!-- markdownlint-disable MD024 -->
# HDR detection and model resolution

- **HDR detection is fail-safe to SDR (ADR-0300).**
  `hdr.detect_hdr` returns `None` on any classification ambiguity
  (missing file, ffprobe failure, malformed JSON, mismatched
  primaries vs. PQ/HLG transfer). Misclassifying SDR as HDR is
  dangerous failure mode (would inject mismatched signaling into
  Rec.709 encode); misclassifying HDR as SDR is recoverable. Do not
  relax BT.2020 primaries gate in `_classify_payload` without
  ADR superseding 0261.
- **HDR codec dispatch table is contract for codec adapters.**
  `hdr.hdr_codec_args` dispatches per `encoder` name. When new
  codec adapter (libx265, libsvtav1, ...) lands under
  `codec_adapters/`, it inherits dispatch row that already exists;
  adapters do not roll their own HDR flag set.
- **`select_hdr_vmaf_model` falls back silently.** When
  `model/vmaf_hdr_*.json` is absent (current state — fork hasn't
  ported Netflix's HDR model yet), `_resolve_vmaf_model` logs
  warning and returns SDR model. Do not change this to raise —
  HDR encode-side correctness ships independently of HDR scoring.
- **`model/vmaf_hdr_model_card.md` is documentation, not weights**
  ([research-0089](../../../docs/research/0089-hdr-vmaf-model-search.md);
  ADR-0300 status update 2026-05-09). File is `.md`, not `.json`,
  so `select_hdr_vmaf_model`'s `vmaf_hdr_*.json` glob does **not**
  match it and continues to return `None`. Do not rename card to
  `.json`, do not relax resolver glob to also match `.md`, and do not
  synthesise placeholder weights. SDR-fallback path with one-shot
  warning is deliberate Path C outcome until either Netflix
  open-sources `vmaf_hdr_v0.6.1.json` upstream or fork acquires
  permissively-licensed HDR-MOS-labelled training corpus.
- **HDR is resolved once per source in `corpus.iter_rows`** (HP-2,
  ADR-0300 status update 2026-05-08). `_resolve_hdr` returns
  `(HdrInfo | None, forced: bool)`; `hdr_codec_args` runs once and
  resulting argv tail rides on every cell's
  `EncodeRequest.extra_params`. Do **not** re-probe ffprobe per
  cell (would burn ffprobe per encode for constant signal), and do not
  move HDR-mode resolution into `_row_for` (decision drives
  encode argv, so it must precede encode). One-shot
  HDR-VMAF-model warning fires once per `iter_rows` invocation via
  `score_model_warned` mutable flag — keep that semantics or
  operators get N spurious warnings on single corpus run.

- **HDR VMAF model resolution goes through
  ``hdr.select_hdr_vmaf_model``.** Canonical filename is
  ``vmaf_hdr_v0.6.1.json`` (Netflix's research-artefact name).
  Route lookups through ``hdr_model_name_for(transfer)`` so
  future Dolby-Vision-specific model entry is one dispatch-table
  row away. "HDR model not shipped" warning is single-shot per
  process; clear it from tests via
  ``hdr.reset_hdr_model_warning()``.
Phase A (this scaffold): grid sweep + JSONL emit. Codecs wired so
far: `libx264` (ADR-0237) and `libsvtav1` (ADR-0294). Phases B–F
per ADR-0237 are explicitly out of scope here; do not add bisect /
predictor / ladder / MCP code into this tree without ADR-0237
follow-up promoting corresponding phase.
Phase A (corpus scaffold): grid sweep + JSONL emit, x264 only.
Phase E (this scaffold): per-title bitrate-ladder generator (Pareto
hull + manifest emit), sampler-pluggable, smoke-only until Phase B
merges. Phases B / C / D / F per ADR-0237 are explicitly out of
scope here; do not add bisect / predictor / per-shot / MCP code
into this tree without ADR-0237 follow-up promoting corresponding
phase.
