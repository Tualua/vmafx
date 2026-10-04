# External benchmark wrappers

`tools/external-bench/compare.py` runs the fork's tiny-AI predictors next
to external quality predictors through wrapper scripts. The harness is for
head-to-head experiments against locally installed competitors; it does not
vendor external model code or weights.

## Competitor keys

Use these keys with `--competitors` and expect the same value in each wrapper
payload's `summary.competitor` field:

| Key | Predictor |
| --- | --- |
| `fork-fr-regressor` | In-tree full-reference regressor wrapper |
| `fork-nr-metric` | In-tree `nr_metric_v1` no-reference wrapper |
| `x264-pvmaf` | Operator-installed Synamedia/Quortex x264-pVMAF |
| `dover-mobile` | Operator-installed DOVER-Mobile |

The key is the schema identity. Model version details belong in optional
metadata or wrapper logs. If a wrapper writes a display label such as
`fork-nr-metric-v1` into `summary.competitor`, `compare.py` rejects that
result before aggregation so the report cannot mix incompatible labels.

## Smoke command

```bash
.venv/bin/python -m pytest tools/external-bench/tests/ -q
```

The tests stub external binaries and use a fake `vmaf-tune` for the fork
wrappers, so the smoke does not require `x264-pVMAF` or DOVER-Mobile to be
installed.

## Operator run

```bash
python3 tools/external-bench/compare.py \
  --competitors fork-fr-regressor fork-nr-metric dover-mobile \
  --bvi-dvc-root ~/.corpus/bvi-dvc \
  --netflix-public-root .corpus/netflix \
  --out-json /tmp/external-bench.json
```

| Flag | Meaning |
| --- | --- |
| `--competitors KEY...` | Subset of the competitor keys above; default all four. |
| `--bvi-dvc-root PATH` | BVI-DVC corpus root (test fold expected at `<root>/test/`). |
| `--netflix-public-root PATH` | Netflix Public Drop root (`<root>/<src>/{ref,dis}/*.yuv`). |
| `--out-json PATH` | Write the aggregated comparison as JSON in addition to the table on stdout. |
| `--limit N` | Truncate the corpus to the first N items, for smoke runs. |

The command exits 4 and prints the expected corpus paths when no corpus is
found. The fork ships neither corpus.

See [`tools/external-bench/README.md`](../../tools/external-bench/README.md)
for the full wrapper-only licence boundary and external install steps.
