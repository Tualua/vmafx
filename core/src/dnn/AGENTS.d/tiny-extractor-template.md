---
paths:
  - core/src/dnn/tiny_extractor_template.h
  - docs/ai/extractor-template.md
invariant: Tiny-AI feature extractors must use shared template helpers and return ENOSYS on disabled-DNN builds.
---
<!-- markdownlint-disable MD013 -->
# Tiny-AI Extractor Template and Feature Extractors

- **Tiny-AI extractor template is dedup contract**
  ([ADR-0250](../../../../docs/adr/0250-tiny-ai-extractor-template.md)).
  New tiny-AI feature extractors use helpers in
  [`tiny_extractor_template.h`](../tiny_extractor_template.h)
  (`vmaf_tiny_ai_require_runtime` /
  `vmaf_tiny_ai_resolve_model_path` / `vmaf_tiny_ai_open_session` /
  `vmaf_tiny_ai_yuv8_to_rgb8_planes` /
  `vmaf_tiny_ai_yuv_to_rgb8_planes` /
  `VMAF_TINY_AI_MODEL_PATH_OPTION`).
  Each extractor calls `vmaf_tiny_ai_require_runtime()` after
  pixel-format / bit-depth validation and before model-path probing so
  disabled-DNN builds return ADR-0374 `-ENOSYS` contract instead
  of misleading missing-model `-EINVAL`.
  User-facing log lines (`<name>: no model path …`, `<name>:
  vmaf_dnn_session_open(<path>) failed: <rc>`) are wire-format-stable
  across extractors — downstream tooling greps them. Never introduce
  per-extractor variants of path / session-open shape; if
  contract needs to change, update helpers in one place. Recipe
  lives in
  [`docs/ai/extractor-template.md`](../../../../docs/ai/extractor-template.md).

- **MobileSal (T6-2a, PR #208 open, ADR-0218 placeholder)** —
  saliency feature extractor; opens session via `vmaf_dnn_*`.
- **TransNet V2 (T6-3a + real weights, ADR-0223 + ADR-0261)** —
  shot-boundary detector with real upstream weights; uses
  bounded-Loop guard from ADR-0171.
- **FastDVDnet (T6-7 / T6-7b, ADR-0215 + ADR-0255)** —
  5-frame window pre-filter; same DNN session contract.
