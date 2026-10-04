- **The tiny model cards quote the terms of the data each model was trained
  on.** `fr_regressor_v1` to `v3`, `vmaf_tiny_v1` to `v4`, `nr_metric_v1`,
  `learned_filter_v1`, `saliency_student_v1` and `v2` and `lpips_sq_v1` were
  trained on the Netflix Public Dataset, KoNViD-1k, BVI-DVC, DUTS-TR or
  ImageNet-derived weights. Each card now quotes those datasets' terms as the
  datasets state them, marks the research-only limits, and states the fork's
  reading for shipping the weights as the fork's own. Two earlier descriptions
  that no dataset page supports were withdrawn. The models stay; RC9 retrains
  them on data cleared for redistribution
  ([ADR-1570](docs/adr/1570-tiny-model-dataset-terms-retrain-rc9.md),
  [dataset terms](docs/ai/training-data.md#dataset-terms)).
