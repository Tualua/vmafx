---
paths:
  - core/src/dnn/model_loader.c
  - core/test/dnn/test_model_loader.c
invariant: VMAF_TINY_MODEL_DIR canonicalizes paths and rejects escapes, and oversized sidecars fail closed before reads.
---
<!-- markdownlint-disable MD013 -->
# Model Directory Path Jail and File Hardening

- **`VMAF_TINY_MODEL_DIR` is optional path jail**. When env var
  set, `model_loader.c` canonicalises requested ONNX path,
  requires it to sit below canonicalised jail directory before any
  model stat/read. Missing jail dirs, non-directory jail paths,
  sibling-prefix escapes, and symlink escapes fail closed with
  `-EACCES`; keep regression cases in
  [`test_model_loader.c`](../../../test/dnn/test_model_loader.c) together
  with any loader changes.

- **Oversized sidecars are rejected before stdio reads.**
  `vmaf_dnn_sidecar_load()` performs `vmaf_path_info_utf8()` size check before
  `vmaf_fopen_utf8()` / `fseek()` / `ftell()`. model and jail paths likewise
  use `vmaf_fullpath_utf8()` before UTF-8-aware metadata/open operations. Keep
  those preflight operations on same Windows UTF-8 contract (ADR-1182) and
  keep exact-name Win64 regression in `test_model_loader.c`. Keep metadata-only guard:
  oversized-sidecar regression expects `-EFBIG` without entering
  normal JSON read path.
