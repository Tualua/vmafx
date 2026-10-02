---
paths:
  - ai/src/vmaf_train/codec.py
  - ai/scripts/train_fr_regressor_v2.py
  - model/tiny/fr_regressor_v2.onnx
invariant: fr_regressor_v2 codec block layout: 6-slot encoder one-hot + preset_norm + crf_norm; append-only vocabulary.
---
<!-- markdownlint-disable MD013 MD060 -->
# Codec-aware fr_regressor_v2

- [ADR-0235](../../docs/adr/0235-codec-aware-fr-regressor.md) — codec-aware FR regressor (`fr_regressor_v2`). `CODEC_VOCAB` in [`src/vmaf_train/codec.py`](../src/vmaf_train/codec.py) is **closed and order-stable** — index of each codec = one-hot column index baked into trained ONNX. Adding codec appends to tuple, bumps `CODEC_VOCAB_VERSION`; reordering silently invalidates every shipped `fr_regressor_v2_*.onnx`. `FRRegressor(num_codecs=0)` must remain v1 single-input contract — flipping default would break every existing `model/tiny/fr_regressor_v1.onnx` consumer. Feature-dump scripts emit `codec` column tagged at call site (BVI-DVC: `"x264"`, Netflix Public: `"unknown"`); never silently default to codec that doesn't match what script encoded.

## fr_regressor_v2 — codec block layout (ADR-0272)

`ai/scripts/train_fr_regressor_v2.py` consumes vmaf-tune Phase JSONL corpus, emits `model/tiny/fr_regressor_v2.onnx`. Codec
block layout is **load-bearing** — bumping it requires re-train.
Pinned invariants:

- `ENCODER_VOCAB = ("libx264", "libx265", "libsvtav1", "libvvenc",
  "libvpx-vp9", "unknown")`. Order matches encoder-onehot index
  baked into trained ONNX. Append-only; bump
  `ENCODER_VOCAB_VERSION` and re-train when adding new entry.
- 8-D codec block layout: `[encoder_onehot[0..5], preset_norm,
  crf_norm]`. Both `preset_norm` and `crf_norm` live in `[0, 1]`.
- `crf_norm = crf / 63.0` — `63` = union upper bound across
  supported encoders (libsvtav1 / libvpx-vp9 max).
- `preset_norm = preset_ordinal / 9.0` — per-encoder ordinal table
  in `train_fr_regressor_v2.py::PRESET_ORDINAL`. libsvtav1's numeric
  0..13 presets squashed to 0..9.
- Two-input ONNX: `features` (N, 6) + `codec` (N, 8) -> `score` (N,).
  Mirrors LPIPS-Sq two-input precedent (ADR-0040 / ADR-0041).
- StandardScaler applied to `features` only; codec block
  passes through unscaled. `feature_mean` / `feature_std` ship in
  sidecar JSON.

Current shipped ONNX is from `--smoke` mode and is registered
`smoke: true` in `model/tiny/registry.json`. Production training run
is gated on multi-codec Phase corpus + per-frame feature emission
in Phase schema. See ADR-0272 + Research-0054.
