---
paths:
  - ai/scripts/train_fr_regressor_v3.py
  - ai/src/vmaf_train/codec.py
  - model/tiny/fr_regressor_v3.onnx
invariant: v3 retrain: 16-slot encoder vocabulary append-only; mean LOSO PLCC >= 0.95 and +0.005 lift floor over v1.
---
<!-- markdownlint-disable MD013 MD060 -->
# v3 retrain invariant — `ENCODER_VOCAB` 13 → 16 (ADR-0302)

`ENCODER_VOCAB_V3` parallel constant in
[`scripts/train_fr_regressor_v2.py`](../scripts/train_fr_regressor_v2.py)
documents target 16-slot vocab (adds `libsvtav1`,
`h264_videotoolbox`, `hevc_videotoolbox` to v2's 13 slots). **Live
`ENCODER_VOCAB` and `ENCODER_VOCAB_VERSION = 2` are source of truth**
until follow-up retrain PR clears LOSO PLCC ship gate.

**Invariants v3 retrain PR must honour** (per ADR-0235 +
ADR-0291 + ADR-0302):

- Schema bump (v2 → v3) requires fresh LOSO run clearing **mean
  LOSO PLCC ≥ 0.95** across all 9 Netflix sources (matches gate
  ADR-0291 cleared on v2). Trainer must exit non-zero and refuse
  to overwrite registry entry on failure — same pattern
  `fr_regressor_v1` already enforces.

  **Status (ADR-0323, 2026-05-06):** First v3 LOSO run shipped
  under [`ai/scripts/train_fr_regressor_v3.py`](../scripts/train_fr_regressor_v3.py)
  on NVENC-only Phase corpus (5,640 rows, 9 sources × 4 CQs).
  Mean LOSO PLCC = **0.9975 ± 0.0018** (every source above 0.99) —
  comfortably clears 0.95 ship gate. Model ships under
  `model/tiny/fr_regressor_v3.onnx` with `smoke: false`. Live
  `ENCODER_VOCAB_VERSION = 2` in [`scripts/train_fr_regressor_v2.py`](../scripts/train_fr_regressor_v2.py)
  **stays authoritative for `fr_regressor_v2.onnx`** until separate
  "promote v3 to authoritative" PR — this PR ships v3 as parallel
  checkpoint, not v2 replacement. Future v3 retrains (on
  multi-codec corpus drop) must continue to clear 0.95 floor and
  must additionally measure ADR-0235 multi-codec lift floor
  (≥+0.005 PLCC over `fr_regressor_v1`); lift floor not yet
  measurable on NVENC-only corpus, so this PR's gate = 0.95
  floor only.
- Multi-codec lift over v1 single-input regressor must remain
  **≥ +0.005 PLCC**. ADR-0235 set this as codec-block invariant;
  v2 production checkpoint cleared it comfortably and v3 must
  not regress.
- In-tree v2 ONNX (`model/tiny/fr_regressor_v2.onnx`) **must
  not be replaced** until new v3 ONNX clears gate. Load-fallback
  shim collapses unknown v3 strings into v2
  `unknown` column and lets v2 keep serving every consumer in   meantime.
- Append-only ordering is load-bearing — 13 v2 slot indices
  (0..12) keep their column positions verbatim under v3; three
  new slots append at indices 13/14/15. Reordering silently
  invalidates every shipped `fr_regressor_v2_*.onnx`. ADR-0235
  documents this rule for `CODEC_VOCAB`; ADR-0302 §Decision
  re-asserts it for `ENCODER_VOCAB`.
- Slot strings must match vmaf-tune codec-adapter registry keys
  exactly (`libsvtav1`, `h264_videotoolbox`, `hevc_videotoolbox`).
  ADR-0235 §References pins this rule globally for all corpus
  emitters.
