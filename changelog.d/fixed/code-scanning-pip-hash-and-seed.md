- **The SBOM jobs unpack the wheel they just built instead of installing it unhashed, and the trainer's seeding no longer swallows an `ImportError`.**
  `release-dry-run.yml` and `supply-chain.yml` take the built `vmaf_mcp` wheel into the SBOM root with
  `python -m zipfile -e`; its dependencies stay installed under `--require-hashes`, and a build
  has no published hash for the wheel itself (OpenSSF Scorecard Pinned-Dependencies, alert 1462).
  `_set_seed()` in `ai/src/vmaf_train/predictor_train.py` asks `importlib.util.find_spec()` whether
  numpy and torch exist instead of catching `ImportError` around the import (CodeQL
  `py/empty-except`, alerts 1489 and 1490), so a broken installed package now raises.
