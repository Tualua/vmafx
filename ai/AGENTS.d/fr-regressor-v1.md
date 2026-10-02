---
paths:
  - ai/scripts/train_fr_regressor.py
  - ai/src/vmaf_train/models/fr_regressor.py
  - model/tiny/fr_regressor_v1.onnx
invariant: C1 baseline: canonical-6 float32 input, mean LOSO PLCC >= 0.95 vs vmaf_v0.6.1 required to ship.
---
<!-- markdownlint-disable MD013 MD060 -->
# `fr_regressor_v1` (C1 baseline — ADR-0249)

Wave-1 C1 baseline trainer =
[`ai/scripts/train_fr_regressor.py`](../scripts/train_fr_regressor.py). It
consumes `runs/full_features_netflix.parquet` (produced by
`ai/scripts/extract_full_features.py` over local Netflix Public
drop at `.corpus/netflix/`), runs 9-fold leave-one-source-out
(LOSO), exports `model/tiny/fr_regressor_v1.onnx` only when mean
LOSO PLCC ≥ 0.95 against `vmaf_v0.6.1` per-frame teacher.

**Contract row** (do not regress without ADR amendment):

- **Input** — `[N, 6]` float32, feature order
  `(adm2, vif_scale0, vif_scale1, vif_scale2, vif_scale3, motion2)`,
  standardised with per-feature `feature_mean` / `feature_std`
  vectors pinned in sidecar JSON. Standardisation is **not**
  baked into ONNX so callers can swap feature pools without
  re-export.
- **Output** — `[N]` float32, VMAF-scale (0–100 typical).
- **Architecture** — stock `vmaf_train.models.FRRegressor` with
  Wave-1 spec hparams (hidden=64, depth=2, dropout=0.1, GELU). Larger
  / smaller variants must register new model id, not overwrite this
  one.
- **Ship gate** — mean LOSO PLCC ≥ 0.95 vs `vmaf_v0.6.1`. Trainer
  exits 3, refuses to overwrite registry on failure; lowering
  threshold = soft-fail of policy, not code change.

**Rebase-sensitive invariants:**

- Canonical-6 feature order is load-bearing — `vmaf_v0.6.1`
  consumes same six features in same order, and ONNX
  graph weight matrix column-aligned to it. Reordering
  sidecar `feature_order` field invalidates checkpoint.
- Refresh PRs must point `--parquet` at current dated Netflix
  full-feature table (e.g.
  `runs/full_features_netflix_refresh_20260520.parquet`), not
  stale historical `runs/full_features_netflix.parquet`, and must
  update model card with new LOSO fold metrics. If
  `torch.onnx.export` leaves orphan `<model>.onnx.data` while
  saved ONNX has no external initializers, restore orphan file and
  commit only inline ONNX plus sidecar/registry changes.
- Netflix Public Dataset is non-redistributable. CI cannot retrain
  end-to-end; only smoke path
  (`python ai/scripts/train_fr_regressor.py --epochs 3 --no-export`)
  runs in CI when parquet is locally available.
