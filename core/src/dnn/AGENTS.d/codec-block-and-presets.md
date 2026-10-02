---
paths:
  - core/src/dnn/model_loader.c
  - core/src/dnn/model_loader.h
invariant: Codec block layout and preset ordinal mapping exactly mirror Python trainer constants and sidecar vocabularies.
---
<!-- markdownlint-disable MD013 -->
# Codec Block Layout and Preset Ordinals

- **Pre-seeded "unknown" codec one-hot** in
  `dnn_attach_feature_vector`: when rank-2 model declares
  second input, scratch buffer's third-from-last slot set
  to 1.0. "Third-from-last" rule mirrors v2 layout
  (`[encoder_onehot…, preset_norm, crf_norm]`) —
  "unknown" one-hot lives at index `N-3`. Any future trainer
  shipping different second-input layout (e.g. inserts
  new normalised feature between one-hot and `preset_norm`)
  must keep "unknown" slot reachable by this offset OR
  update loader to honour explicit sidecar
  `unknown_encoder_index` field.

## Invariant — `PRESET_ORDINAL` mirrors Python trainer (ADR-0519)

`model_loader.c::codec_block_preset_ordinal()` is verbatim port of
`ai/scripts/train_fr_regressor_v2.py::PRESET_ORDINAL` (lines
169..234). When trainer adds encoder (e.g. AMD AMF in
ADR-0302 v3 retrain) or changes preset ordinal, C-side table
must update in same PR. Otherwise codec block populated by
`--tiny-codec` diverges from what model was trained against.

`PRESET_MAX_ORDINAL = 9.0` and `CRF_MAX = 63.0` constants are
shared invariants between two files; they appear inline in C
helper rather than as named constants so `grep '/ 9.0f'` /
`'/ 63.0f'` finds them.

Encoder vocabulary itself comes from sidecar's
`encoder_vocab` array (loaded into `VmafModelSidecar.encoder_vocab[]`),
not from duplicated C-side constant, so vocab bumps only require
new sidecar JSON — no C recompile.

## Invariant — codec block layout (ADR-0522)

Second input of `fr_regressor_v2` is exactly
`[encoder_onehot(N_VOCAB), preset_norm, crf_norm]`. Runtime
guards check `extra_in_width == n_encoder_vocab + 2u` at attach time
*and* in `vmaf_ctx_dnn_set_codec_context` bridge; both checks
must agree. If future codec-aware model uses different layout
(e.g. multi-scale codec mixing), bump sidecar
`codec_block_layout` array, add dispatch branch — never silently
extend `vmaf_dnn_codec_block_fill` to different layout.
