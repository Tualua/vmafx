- **`vmaf-tune`'s predictor trainer exports ONNX through the shared exporter.**
  `predictor_train._export_onnx()` called the TorchScript exporter, deprecated
  since torch 2.9, and failed under warnings-as-errors with torch installed.
  It now calls `vmaf_train.models.exports.export_to_onnx()` like the other tiny
  model trainers: torch.export based, dynamic batch axis, op-allowlist and
  onnxruntime round-trip checks. The exported graph keeps the input name
  `input` and the output name `vmaf`; its batch axis is now dynamic.
