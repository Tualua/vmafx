- **`fr_regressor_v3` receives the codec block it was trained on.** libvmaf
  normalised every codec-aware model's preset and CRF slots the way
  `fr_regressor_v2` was trained (preset ordinal / 9, CRF / 63); v3 was trained
  with `preset_norm` 0.5 on every row and the CRF min-max normalised over
  19..37. A sidecar can now declare its normalisation (`codec_preset_norm`,
  `codec_crf_norm` and bounds), `fr_regressor_v3.json` does, and an encoding
  libvmaf cannot reproduce refuses the model. With v3, `--tiny-preset` has no
  effect and the run says so
  ([ADR-1558](docs/adr/1558-codec-block-encoding-from-sidecar.md)).
