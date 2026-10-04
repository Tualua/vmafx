<!-- markdownlint-disable MD013 MD060 -->
# ADR-1527: TransNet V2 runs upstream's 100-frame windows on 0..255 thumbnails and binds its output by position

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: ai, dnn, tiny-ai, correctness, fork-local

## Context

The `transnet_v2` extractor could not run the shipped model at all
(docs-audit defect 24): `vmaf_dnn_session_open()` probed the input shape with a
rank limit of 4 and returned `-ERANGE` for TransNet's rank-5 input, and the
extractor bound an output named `boundary_logits` while the tf2onnx export names
it `output_0`.

Making it open showed two more faults that each made the detector miss every
cut. The thumbnails were scaled to 0..1, but upstream feeds 0..255 frames and
its ColorHistograms branch casts them to integers and bins with `>> 5`; with
0..1 input the network's highest probability on a hard cut between the Netflix
`src01` clip and the BBB clip was 0.054. And the extractor read the logit of
the window's last slot, the frame just read: that frame has no later frame in
the window, and on the same cut its probability was 0.10 even with 0..255
input. Upstream's `predict_frames()` pads the clip (25 copies of the first
frame in front, copies of the last behind), runs 100-frame windows advancing
by 50 and keeps slots 25..74, so every frame has at least 25 frames of context
on each side; with that scheme the frame before the cut scores 0.89. The
extractor also had no `VMAF_FEATURE_EXTRACTOR_TEMPORAL` flag, so a thread pool
or `--subsample` would hand its ring frames out of order.

## Decision

We will reproduce `predict_frames()` in the extractor: thumbnails in 0..255
(samples above 8 bits scaled by 255 / (2^bpc - 1)), a ring keyed by frame
index, window k (frames `50k - 25 .. 50k + 74`) run when frame `50k + 74` is
read and written to frames `50k .. 50k + 49`, the remaining windows run in
`flush()` with the last frame repeated, and the extractor flagged temporal. The
one output is bound by position. The session-open probe reads ranks up to 8
and treats a larger rank as "not a luma fast-path model" instead of failing.
The sidecar and the exporter state the real output name, `output_0`.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Upstream windows with delayed output (chosen) | Same predictions as upstream on the same thumbnails; one inference per 50 frames | Features are written up to 74 frames late, the last ones at flush | — |
| One window per frame, frame at slot 74 | Fixed 25-frame delay | 50 times the inference cost; context differs from upstream, so its numbers are not upstream's | Cost, and no reference to check against |
| Keep reading the last slot | Per-frame output while reading | Misses hard cuts (0.10 on a hard cut) | Wrong result |
| Rename the graph output to `boundary_logits` | Matches the old documented contract | Changes a 30 MB model file and its registry hash; the exporter would have to rename too | Binding by position works for any exporter's name |
| Decode RGB and resize as ffmpeg does | Upstream's exact input | Needs a colour conversion and a bicubic resize in C; luma already scores 0.89 on the test cut | Recorded as a follow-up in the model card |

## Consequences

- **Positive**: the shipped model opens, runs once per 50 frames and marks cuts
  (synthetic clips: p > 0.99 on every cut frame, < 0.16 elsewhere).
- **Negative**: `shot_boundary*` of a frame appear up to 74 frames after it is
  read, the last ones only at flush; a caller polling per-frame scores while
  reading sees them late.
- **Neutral / follow-ups**: RGB decode and bilinear or bicubic resize remain a
  follow-up (T6-3c in the model card).

## References

- `Q` (maintainer popup, 2026-10-04, paraphrased): fix every docs-audit defect
  now; TransNet V2 loads and runs (rank-5 input through the DNN path, output
  name bound correctly) with a test on a tiny clip.
- Docs-audit defect 24.
- Upstream inference: `soCzech/TransNetV2` `inference/transnetv2.py` on
  `master` (read 2026-10-04): `predict_frames()` pads 25 frames in front and
  `25 + 50 - (len % 50 or 50)` behind, steps windows of 100 by 50 and keeps
  `[25:75]`; `predictions_to_scenes()` ends a scene at the frame whose
  prediction is 1.
- [ADR-0223](0223-transnet-v2-shot-detector.md),
  [ADR-0261](0261-transnet-v2-real-weights.md).
