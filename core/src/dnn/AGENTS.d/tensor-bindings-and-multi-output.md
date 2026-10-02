---
paths:
  - core/src/dnn/dnn_attach_api.c
  - core/src/dnn/ort_backend.c
invariant: Multi-input graphs bind by ONNX name and attached multi-output models emit scalar collector keys in priority order.
---
<!-- markdownlint-disable MD013 -->
# Tensor Bindings and Attached Multi-Output Naming

- **Tensor bindings are named**: multi-input graphs bind by ONNX input name
  when `VmafDnnInput::name != NULL`; positional fallback is for single-input
  legacy paths only. See
  [ADR-0040](../../../../docs/adr/0040-dnn-session-multi-input-api.md).

## Invariant — attached scalar multi-output naming (ADR-0646)

`vmaf_use_tiny_model()` / `vmaf_ctx_dnn_attach()` preserve old
single-output collector key exactly: sidecar `name` (or
`vmaf_tiny_model`) without appended output suffix. Multi-output
attached models route through `vmaf_ort_run()`, publish one feature
collector key per scalar ONNX output. Suffix source order is:

1. sidecar `output_names[]` when array count equals ONNX output
   count;
2. ONNX graph output name;
3. deterministic `output<slot>_<attempt>` fallback after sanitisation or
   duplicate collapse.

Attached path is intentionally scalar-only. Never flatten vector or
image tensors into feature names during rebase; that needs new ADR
because it changes report schema cardinality. Also never revert
rank-2 / rank-4 frame runners back to `vmaf_ort_infer()` — that helper
is single-output by construction, would reopen T-DNN-MULTI-OUTPUT.
