- **The two PTQ stub scripts are removed.** `ai/scripts/gen_calibration.py` and
  `ai/scripts/quantize_int8.py` only printed "not yet implemented" and exited 1. Static PTQ is
  `vmaf-train quantize-int8`, which calibrates from a parquet feature cache
  ([quantization guide](docs/ai/quantization.md)).
