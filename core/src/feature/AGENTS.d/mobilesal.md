---
paths:
  - core/src/feature/feature_mobilesal.c
  - core/test/test_mobilesal.c
  - core/test/dnn/test_mobilesal_run.c
invariant: MobileSal extractor pads sides to a multiple of 8 for the students and averages only the frame's own area.
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

- **Padding to a multiple of 8 ([ADR-1540](../../../../docs/adr/1540-saliency-pad-to-multiple-of-8.md))** —
  `mobilesal_init()` sizes every buffer for `pw` / `ph`, the frame size
  rounded up to `MOBILESAL_SIZE_MULTIPLE` (8: the students halve three
  times and concatenate skips, so 576x324 fails in ORT unpadded).
  `mobilesal_pad_plane()` spreads each RGB plane to stride `pw`, last row
  first, repeating the last column and row; `mobilesal_cropped_mean()`
  averages the frame's own `w x h` of the map, and a map of another size
  than the input is `-EIO`. A frame that already is a multiple of 8 is fed
  unchanged (bit-identical scores). `core/test/dnn/test_mobilesal_run.c`
  guards it with the shipped students.
