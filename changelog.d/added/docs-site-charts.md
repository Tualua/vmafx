- **Charts in the documentation, drawn from repository data.** Three
  Vega-Lite charts render to static SVG in light and dark at docs-generation
  time and turn interactive in the browser, with exact values on hover: the
  status of every GPU twin against the CPU extractor (landing page and
  [Backends](docs/backends/index.md)), the upstream-parity allowlist by
  extractor ([Upstream parity guard](docs/development/upstream-parity.md)) and
  the per-frame VMAF of the 576x324 snapshots
  ([Netflix benchmark baselines](docs/development/netflix-benchmark-baselines.md)).
  Each has a text alternative and its data table.
  `scripts/docs/generate-charts.py` builds them from
  `scripts/ci/exact_twins.d/`, `scripts/ci/upstream_parity.d/` and
  `testdata/`, and `make docs-fragments-check` fails when a render, the data or
  a page block drifts. The renderer, vl-convert-python 1.9.0.post1, is
  hash-pinned in the docs lock; the browser bundle of Vega, Vega-Lite and
  vega-embed is vendored with its licence texts and loads only on chart pages
  ([Documentation site design](docs/development/docs-site-design.md#charts),
  [ADR-1508](docs/adr/1508-docs-site-toolchain-and-charts.md)).
