- **Feature-vector tiny models score the features the run computed.** A
  `--tiny-model` such as `vmaf_tiny_v2` or `fr_regressor_v1` read its input
  features from whatever the run happened to compute and took a missing one as
  0: with the default model alone `vmaf_tiny_v2` printed -0.853 on every
  frame, and even with `--feature adm --feature vif --feature motion` every
  frame after the first read `motion2 = 0`. Loading the model now registers the
  extractors of the features its sidecar names, the model is scored once the
  run is flushed, and a frame that lacks an input fails the run with a message
  naming it; a sidecar that names an unknown feature, or the wrong number of
  them, fails at load
  ([ADR-1520](docs/adr/1520-tiny-model-feature-inputs-at-flush.md)).
  Codec-aware models (`fr_regressor_v2`, `fr_regressor_v3`) no longer score a
  guessed codec block: they need `--tiny-codec` and `--tiny-crf`
  (`vmaf_dnn_set_codec_context()` in the C API) and stop on the first frame
  without them, and the `fr_regressor_v2_ensemble_v1_seed*` models, whose
  sidecars describe another codec block than their graphs take, are refused
  at load. Breaking for C API callers: tiny feature-vector scores exist only
  after `vmaf_read_pictures(ctx, NULL, NULL, 0)`.
