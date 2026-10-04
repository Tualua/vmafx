- **The tiny-model registry validator no longer validates less when `jsonschema` is
  missing.** `ai/scripts/validate_model_registry.py` used to fall back to a
  four-field structural check and print `OK`; it now exits 2 and names the
  install command (`pip install --require-hashes -r requirements/locks/jsonschema.txt`).
  `jsonschema` was already a declared dependency and the CI job installs it, so
  a run that passed before still passes.
