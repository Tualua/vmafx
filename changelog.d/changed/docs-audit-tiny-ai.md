- Corrected the tiny-AI pages and model cards against the code and
  `model/tiny/registry.json`. The tiny-AI index is now an entry page with one
  runnable `--tiny-model` command that links every tiny-AI page. Corrected:
  `vmaf-train` has 15 subcommands, `--tiny-model` takes a path (not a registry
  id), the per-frame output is a feature named after the sidecar, the registry
  has 26 entries, the op allowlist 74, `--feature name=opt=val` replaces the
  nonexistent `--feature_params`, and `fr_regressor_v2` reads a 14-element codec
  block. Cards for feature-vector models warn that the same run must compute
  the features they read.
