- **Preserve explicit int8 paths in tiny-model DNN session loading.** When
  `vmaf_dnn_session_open()` was called with an explicit `.int8.onnx` path,
  `resolve_load_path()` lacked the `kInt8Suffix` early return present in
  `dnn_attach_api.c`, causing it to append a redundant `.int8` suffix and derive
  `<name>.int8.int8.onnx` before falling back to the fp32 path. The resolver now
  checks `kInt8Suffix` upfront and preserves explicit int8 paths directly.
