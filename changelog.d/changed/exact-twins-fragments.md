- The set of GPU twins the parity gate compares exactly is no longer a literal
  in `scripts/ci/cross_backend_calibration.py`: each (feature, backend) is one
  file under `scripts/ci/exact_twins.d/` (`adr:` and `evidence:`), the loader
  validates the directory, and the table in
  `docs/development/cross-backend-exact-twins.md` is generated from it by
  `make docs-fragments-write`. Declaring a twin exact edits no shared file
  (ADR-1428).
