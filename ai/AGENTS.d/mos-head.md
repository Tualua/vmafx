---
paths:
  - ai/scripts/train_konvid_mos_head.py
  - ai/scripts/train_chug_hdr_mos_head.py
  - model/konvid_mos_head_v1*
invariant: MOS head: 11-D feature layout load-bearing; MOS range [1, 5] baked in graph; production-flip gate unchanged.
---
<!-- markdownlint-disable MD013 MD060 -->
# MOS-head v1 invariants — `konvid_mos_head_v1` (ADR-0336, Phase 3 of ADR-0325)

Fork's first head trained against subjective MOS (not VMAF)
ships under
[`ai/scripts/train_konvid_mos_head.py`](../scripts/train_konvid_mos_head.py),
with trained ONNX and human-readable model card at
[`model/konvid_mos_head_v1.onnx`](../../model/konvid_mos_head_v1.onnx) and
[`model/konvid_mos_head_v1_card.md`](../../model/konvid_mos_head_v1_card.md).
Invariants any follow-up retrain or corpus-shape PR must honour:

- **Feature-column order is load-bearing.** `FEATURE_SCHEMA_KONVID_V1`
  maps to `FEATURE_COLUMNS = CANONICAL_6 + EXTRA_FEATURES`, exact
  11-D layout baked into trained KonViD ONNX and consumed by
  `tools/vmaf-tune/src/vmaftune/predictor.py::_predict_mos_via_head`.
  6 canonical columns occupy indices 0..5; 5 extras
  (`saliency_mean`, `saliency_var`, `shot_count_norm`,
  `shot_mean_len_norm`, `shot_cut_density`) occupy 6..10. Reordering
  silently invalidates every shipped `konvid_mos_head_v1.onnx`.
  New experimental layouts must be separate named schemas in
  `FEATURE_SCHEMAS`, not in-place edits to `FEATURE_COLUMNS`.
- **ENCODER_VOCAB v4 expansion is append-only.** v4 vocab ships
  with single `"ugc-mixed"` slot per ADR-0325 §Decision. LSVQ +
  YouTube-UGC ingestion landing -> new slots append at end;
  existing trained ONNX stays loadable and predictor's per-shot
  one-hot widens transparently.
- **MOS range = `[1.0, 5.0]`, baked into graph.** Trainer wraps
  MLP output in `MOS_MIN + (MOS_MAX - MOS_MIN) * sigmoid(raw)`;
  adversarial input cannot drive prediction outside `[1, 5]`.
  Predictor surfaces (`Predictor.predict_mos` +
  `_predict_mos_via_head`) carry additional clamp as
  belt-and-braces. Do not change range without schema bump +
  retrain.
- **Production-flip gate is not lowered on real-corpus failures.**
  Per memory `feedback_no_test_weakening` and ADR-0325 §Production-flip
  gate, failing real-corpus retrain ships head with
  `Status: Proposed`, *not* relaxed gate. Threshold values
  (`PLCC ≥ 0.85`, `SROCC ≥ 0.82`, `RMSE ≤ 0.45`, `spread ≤ 0.005`)
  are constants in trainer (`GATE_*`); changing them requires
  new ADR.
- **Predictor fallback path is documented behaviour, not bug.**
  ONNX missing -> `Predictor.predict_mos` returns
  `(predicted_vmaf - 30) / 14` clamped to `[1, 5]`. That's
  documented contract; tests
  (`tools/vmaf-tune/tests/test_predict_mos.py::test_predict_mos_falls_back_when_onnx_missing`)
  pin it. Removing fallback breaks every dev host that hasn't
  pulled ONNX.
- **CHUG HDR MOS uses CHUG-named entry point.**
  `ai/scripts/train_chug_hdr_mos_head.py` = operator-facing
  command for CHUG HDR subjective-MOS experiments. May reuse   same small MOS-head training loop, but docs and local commands must
  not tell operators to pass CHUG shards through KonViD-named
  flags. Local CHUG manifests use `chug_hdr_mos_head_v1` so HDR
  MOS signal isn't confused with committed SDR KonViD head.   CHUG wrapper defaults to `FEATURE_SCHEMA_CHUG_HDR_WIDE_V1`
  (`chug-hdr-wide-v1`): canonical-6 means, p10/p90/std temporal
  aggregates, and HDR ladder / geometry metadata. Keep that 34-D order
  append-only for local CHUG checkpoints; use `--feature-schema
  konvid-v1` only for ablation runs against older 11-D baseline.
