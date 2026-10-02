---
paths:
  - core/src/dnn/model_loader.c
  - core/src/dnn/model_loader.h
invariant: Sidecar parser supports dual trainer naming schemas with static feature bounds and implicit external data.
---
<!-- markdownlint-disable MD013 -->
# Model Sidecar Schema and Feature Vector Invariants

- **Sidecar parser MUST accept both naming conventions** for
  feature schema: `feature_order` / `feature_mean` / `feature_std`
  (trainer style used by `ai/scripts/train_fr_regressor*.py`)
  AND `features` / `input_mean` / `input_std` (trainer style
  used by `ai/scripts/train_vmaf_tiny_v*.py`). Removing
  either alias silently breaks one of two trainer paths.
  Loader still loads ONNX, but `n_features` stays 0 and
  fallback canonical-6 ordering is used unconditionally,
  scrambling models whose feature order differs from
  canonical-6. `test_sidecar_feature_vector_*` regression
  tests in `test_model_loader.c` gate this.
- **`VMAF_DNN_MAX_FEATURE_NAMES = 32`** is static cap on
  in-struct `feature_names[]` / `feature_mean[]` / `feature_std[]`
  arrays. Cap exists to keep `VmafModelSidecar` heap-free
  (Power-of-10 / no-VLA). Increasing it has no behavioural cost
  but lower-bounds per-context memory; never shrink it
  below 6 (canonical-6).
- **ORT external-data resolution is implicit**. Never add
  `AddExternalInitializersFromFilesInMemory` plumbing —
  `OrtCreateSession(env, abs_path, opts, &session)` already
  resolves sibling `.onnx.data` files. Adding manual external-data
  wiring opens second code path that drifts.
