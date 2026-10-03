- **`testdata/bench_upstream_ab.py` takes its score verdict from the upstream
  parity guard and builds upstream at the recorded parity head** (ADR-1487).
  The `--max-score-delta` option and its `1e-5` ceiling on the six-decimal
  pooled score are gone: the model's values are compared at `%.17g` against
  the allowlist of recorded deviations. `--upstream-ref` still names another
  commit or tag; `--fork-build` names the golden-profile build that is timed
  and checked (default: in the guard's work directory); with `--upstream-bin`
  the parity check is reported as not run. Outside the dev container image
  the verdict is marked advisory.
