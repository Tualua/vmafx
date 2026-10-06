- The four FR regressor trainers (`train_fr_regressor.py`, `_v2.py`,
  `_v2_ensemble.py`, `_v3.py`) write `model/tiny/registry.json` through
  `vmaf_train.registry.write_registry_json()`. A non-finite number in a registry
  row is written as `null` instead of the non-standard `NaN` token no strict JSON
  reader accepts; a registry of finite values is byte-identical to before.
