---
paths:
  - core/src/dnn/dnn_api.c
  - core/src/dnn/model_loader.c
  - core/src/dnn/ort_backend.c
invariant: C translation units suppress modernize-use-nullptr via NOLINT brackets to retain MSVC cl.exe compatibility.
---
<!-- markdownlint-disable MD013 -->
# MSVC C23 Null Pointer Compatibility

## Invariant — every `core/src/dnn/*.c` keeps `NULL` (ADR-1138)

`dnn_api.c`, `dnn_attach_api.c`, `model_loader.c`, `onnx_scan.c`,
`op_allowlist.c` and `ort_backend.c` each carry one file-scoped
`/* NOLINTBEGIN(modernize-use-nullptr) … ADR-1138. */` …
`/* NOLINTEND(modernize-use-nullptr) */` bracket. Bracket is not
cosmetic: `Build — Windows MSVC + CUDA (build only)` is required
status check, compiles these translation units with `cl.exe`.
Its documented `/std:clatest` C23 feature set does not include
`nullptr` keyword. Rewriting `NULL` to `nullptr` here therefore
fails required lane — PR #1192 had to be reverted for exactly
that reason before ADR-1138 was written.

Keep bracket spanning whole file (`NOLINTEND` is last
line), keep ADR citation in comment, add same bracket to
any new `.c` file in this directory rather than using keyword.
`psnr_tools.cpp` and other C++ TUs are unaffected — ADR-0915's
`modernize-use-nullptr` ratchet still applies to them in full.
