- **The model-registry schema tests run in the Python harness suite.** `python/test/model_registry_schema_test.py`
  skipped its whole module when `jsonschema` was not installed. `jsonschema` is now in
  `python/requirements-test.in` and its hash lock, and a missing install is an error, not a skip.
