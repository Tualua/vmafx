---
paths:
  - core/src/dnn/dnn_attach_api.c
  - core/src/dnn/ort_backend.c
  - core/test/dnn/test_vmaf_use_tiny_model.c
invariant: Model attach accepts rank-2 and rank-4 inputs with symbolic batch dimensions folded to 1 at attach time.
---
<!-- markdownlint-disable MD013 -->
# Rank and Symbolic Batch Dimension Acceptance

## ADR-0518 invariants — tiny-model loader accepts rank-2 + external-data ONNX

- **`vmaf_ctx_dnn_attach` accepts `in_rank == 2` AND `in_rank == 4`**.
  Reverting to `!= 4` gate breaks every shipped FR regressor
  (three checkpoints under `model/tiny/fr_regressor_v[12]*.onnx` plus
  `vmaf_tiny_v4`). Rank-2 branch lives in
  `dnn_attach_feature_vector()` (file-static helper in
  `libvmaf.c`); dispatch in `vmaf_ctx_dnn_run_frame` reads
  `vmaf->dnn.in_rank` to route to NCHW vs feature-vector path.

## Invariant — symbolic batch dim acceptance (ADR-0524)

`vmaf_ctx_dnn_attach`'s helpers (`dnn_attach_nchw`,
`dnn_attach_feature_vector`, and optional rank-2 second-input
shape probe) accept `in_shape[0] ∈ {1, -1}` for batch dimension.
ORT reports symbolic ONNX dims as `-1` via C API
(`OrtApi::GetDimensions`). Per-frame inference loop always
emits `shape[0] = 1` on ORT Run call, so symbolic batch is
folded to 1 at attach time. **Never** re-tighten gate to
`!= 1` — that breaks every shipped NR tiny model
(`model/tiny/nr_metric_v1*.onnx`) plus any future trainer using
PyTorch `torch.onnx.export(..., dynamic_axes=…)` default.

*fixed* batch > 1 is still rejected (no batched-inference
scheduler exists; per-frame loop feeds one sample per Run
call). Symbolic H/W (rank-4 spatial dims) remain rejected because
scratch buffer is sized once at attach time; diagnostic
distinguishes "symbolic H/W" from "C != 1" so failure mode is
observable. `test_attach_accepts_symbolic_batch_rank4`
regression in `test_vmaf_use_tiny_model.c` synthesises minimal
rank-4 ONNX with `dim_param='batch'`, gates against accidental
re-tightening.
