- Corrected the C API pages against the public headers and split the overview
  into lifecycle, pictures, and models-and-features pages. The runnable example
  no longer releases pictures after a failed `vmaf_read_pictures` (the context
  owns them) and prints the current score. `api/predictor.md` documents the real
  `vmaftune.predictor` API instead of one that never existed, `api/ladder.md` the
  real `select_knees` / `emit_manifest` signatures, `api/dnn.md` the real
  `vmaf_dnn_verify_signature`, and `api/gpu.md` the implemented D3D11 import and
  the 19 HIP and 17 Metal extractors.
