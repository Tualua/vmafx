---
paths:
  - core/src/dnn/onnx_scan.c
  - core/src/dnn/op_allowlist.c
  - core/src/dnn/op_allowlist.h
invariant: ModelProto wire scanner validates permitted domains and allowlisted ops before ORT session creation.
---
<!-- markdownlint-disable MD013 -->
# ONNX Wire-Format Scanner and Op Allowlist

- **Trust boundary**: any `.onnx` loaded via `--tiny-model` or registry
  is untrusted input. `onnx_scan.c` = gate; `op_allowlist.c` =
  policy; `model_loader.c` does `realpath` + symlink-escape hardening.
  See [ADR-0039](../../../../docs/adr/0039-onnx-runtime-op-walk-registry.md).
- **No skipping scan**: `CreateSession` must not be called before
  `vmaf_dnn_validate_onnx` returns success.

- **Domain check is load-bearing (ADR-1089)**: `onnx_scan.c` now gates
  full `(domain, op_type)` tuple, not op_type alone. `read_domain()` rejects
  any `NodeProto.domain` neither `""` nor `"ai.onnx"`. Never remove
  this check or widen allowed-domain set without new ADR: ORT dispatches
  via `(domain, op_type)`, non-standard domain can shadow allowlisted
  op_type with arbitrary custom-op code. If future consumer requires ONNX-ML
  ops (`"ai.onnx.ml"`), separate ADR must audit full ONNX-ML op set,
  justify expansion.
- **Op-allowlist additions for TransNet V2 (ADR-0257)**:
  `BitShift`, `GatherND`, `Pad`, `Reciprocal`, `ReduceProd`,
  and `ScatterND` are now load-bearing for
  `model/tiny/transnet_v2.onnx` (upstream ColorHistograms +
  FrameSimilarity branches require all six). On rebase: removing
  any of them from `op_allowlist.c` = model-breakage event;
  keep trailing block above `Loop` / `If` control-flow
  block intact. Future tiny-AI models leveraging
  these ops inherit them transparently.
