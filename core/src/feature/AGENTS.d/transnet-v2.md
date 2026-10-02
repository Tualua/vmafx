---
paths:
  - core/src/feature/transnet_v2.c
  - core/src/feature/transnet_v2_score.h
invariant: TransNet V2 100-frame-window buffering and shot-boundary extractor contracts.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# TransNet V2 100-Frame Window Contract

- **`transnet_v2.c` 100-frame-window contract** (fork-local,
  ADR-0223 + ADR-0257) — TransNet V2 shot-boundary detector is
  wired to I/O contract `frames: float32 [1, 100, 3, 27, 48]`
  (100-frame window of 27x48 RGB thumbnails) → `boundary_logits:
  float32 [1, 100]`. Four pieces are load-bearing on rebase:
  (1) ring buffer holds 100 slots and replicates *oldest*
  available frame across pre-clip slots (head-clamp at clip
  start) — corresponding output logit is read from
  `output_logits[WINDOW-1]` because `gather_window` lays
  most-recent push at LAST channel; (2) dual feature-name
  surface — extractor emits both
  `shot_boundary_probability` (sigmoid of centre-slot
  logit) **and** `shot_boundary` (binary 0/1 thresholded at 0.5);
  downstream consumers (per-shot CRF predictor T6-3b,
  FFmpeg shot-cut filter shipping with T6-3b) bind to *both*
  exact strings; (3) shipped ONNX under
  `model/tiny/transnet_v2.onnx` is real upstream weights as of
  ADR-0257 (`smoke: false`, MIT, upstream commit pin
  `77498b8e`); wrapper layer that adapts NTCHW→NTHWC and
  selects only `output_1` lives in
  `ai/scripts/export_transnet_v2.py` and must be re-run if
  upstream commit pin moves; (4) export pipeline replaces
  rank-2 `UnsortedSegmentSum` in upstream's `ColorHistograms`
  branch with equivalent `ScatterND` reduction='add'
  subgraph — semantics-preserving but load-bearing rewrite that
  any future upstream-graph re-conversion has to repeat. See
  [ADR-0223](../../../../docs/adr/0223-transnet-v2-shot-detector.md)
  [ADR-0257](../../../../docs/adr/0257-transnet-v2-real-weights.md).

- **TransNet V2 shot-boundary extractor (T6-3a + T6-3a-followup,
  ADR-0223 + ADR-0257)** — second half of T6-2 bundle. Now ships
  real upstream weights via NTCHW adapter (see
  `transnet_v2.c 100-frame-window contract` invariant above).
- **TransNet V2 shot-boundary extractor (T6-3a + T6-3a-followup,
  PR #210 MERGED, ADR-0223 + ADR-0257)** — second half of T6-2
  bundle, ~1M params. DNN-backed. Ships real upstream weights via
  NTCHW adapter (see `transnet_v2.c 100-frame-window contract`
  invariant above).
