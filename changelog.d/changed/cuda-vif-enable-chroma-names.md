- **`vif_cuda=enable_chroma=true` reports its scores as
  `integer_vif_scale0_enable_chroma` to `integer_vif_scale3_enable_chroma`.**
  The CUDA VIF twin cleared its no-op `enable_chroma` option before naming
  its features, so a run with the option reported the default names
  `integer_vif_scale0` to `integer_vif_scale3`. It now names them from the
  options the caller set, as every other extractor does. The scores do not
  change (VIF stays luma-only), and a run without the option keeps the
  default names. A script that reads the old names from an
  `enable_chroma=true` run needs the suffix
  ([ADR-1836](docs/adr/1836-cuda-vif-enable-chroma-names.md)).
