---
paths:
  - core/src/feature/feature_mobilesal.c
  - core/test/test_mobilesal.c
invariant: MobileSal saliency extractor smoke-only gating and ONNX runtime integration.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# MobileSal Saliency Extractor Smoke-Only Gating

- **MobileSal saliency extractor (T6-2a, PR #208 open, ADR-0218
  placeholder)** — first half of T6-2 (encoder-side ROI bundle).
  DNN-backed; opens sessions through
  [`../dnn/`](../../dnn/AGENTS.md).
- **MobileSal saliency extractor (T6-2a, ADR-0218; smoke-only
  placeholder shipped, real-weights swap deferred per
  [ADR-0257](../../../../docs/adr/0257-mobilesal-real-weights-deferred.md)
  and [ADR-0265](../../../../docs/adr/0265-u2netp-saliency-replacement-blocked.md))**
  — first half of T6-2 (encoder-side ROI bundle). DNN-backed;
  opens sessions through [`../dnn/`](../../dnn/AGENTS.md). Two
  real-weights swap attempts blocked: upstream MobileSal is
  CC BY-NC-SA 4.0 + Google-Drive-walled + RGB-D (ADR-0257), and
  recommended U-2-Net `u2netp` replacement is also
  Google-Drive-walled and uses ONNX `Resize` which is not on
  fork's `op_allowlist.c` (ADR-0265). C-side `input` →
  `saliency_map` tensor-name contract is invariant across both
  blockers; any future drop-in replaces `.onnx` and bumps
  registry sha256 without touching this file.
