- **Sidecars name the ONNX opset with one key, `opset`.** The C model loader read `onnx_opset`,
  a key only seven sidecars carried, while the registry, its schema and its validator use `opset`;
  `vmaf_model_meta.opset` stayed 0 for the rest. The loader, every shipped sidecar, the sidecar
  writers and `vmaf_train.registry.ModelMetadata` (field `onnx_opset` is now `opset`) agree.
  Migration: rename `onnx_opset` to `opset` in out-of-tree sidecars; the loader no longer reads
  the old key, and `ModelMetadata` refuses a sidecar that has it.
