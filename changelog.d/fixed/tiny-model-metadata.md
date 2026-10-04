- **Tiny-model metadata matches the shipped graphs, and CI keeps it so.**
  `nr_metric_v1` recorded opset 17 for files that import 18; the
  `fr_regressor_v2` notes described an 8-D codec block for a 14-wide input and
  the trainer's defaults built a smaller model than the shipped 3 x 32 one; the
  five `fr_regressor_v2_ensemble_v1_seed*` sidecars described other graphs;
  `transnet_v2.json` named an output the graph does not have; and
  `registry.schema.json` described a runtime digest check that does not exist
  and rejected `release_url`. The metadata now follows the graphs, the
  exporters record the opset the file imports, the `fr_regressor_v2` trainer
  defaults to `--hidden 32 --depth 3`, and
  `ai/scripts/validate_model_registry.py` reads every registered graph (no
  `onnx` package needed) and fails on a mismatch
  ([ADR-1546](docs/adr/1546-tiny-model-metadata-against-graphs.md)). The
  `ai/scripts/build_calibration_set.py` stub is removed; `vmaf-train
  quantize-int8` calibrates static PTQ from a parquet feature cache.
