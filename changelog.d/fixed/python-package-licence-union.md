- **Every Python package declares the licences of the files it ships.**
  `vmaf` (the Python harness), `vmaf-train`, `vmaf-dev-llm`, `vmaf-roi-score`
  and the ensemble training kit declared `BSD-2-Clause-Patent` for files that
  are EUPL-1.2 or a mix, and shipped no licence texts; `vmaf-mcp` did not count
  the repository `.gitignore` its sdist carries. Each package's PEP 639
  `License-Expression` is now the union of the licences of its sdist and wheel
  files (`EUPL-1.2 AND BSD-2-Clause-Patent` for the hatch packages,
  `BSD-2-Clause-Patent AND BSD-2-Clause AND BSD-3-Clause-Clear AND EUPL-1.2`
  for `vmaf`, whose compiled ADM extension includes EUPL-1.2 headers), each
  text ships in its `LICENSES/`, and `python/test/setup_metadata_test.py`
  recomputes the union for every package instead of requiring
  `BSD-2-Clause-Patent`, which had failed on `master` since #1954
  ([ADR-1560](docs/adr/1560-python-package-licence-union.md),
  [licensing](docs/licensing.md#python-packages)).
