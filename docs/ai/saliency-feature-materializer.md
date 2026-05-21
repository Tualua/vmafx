# Saliency Feature Materializer

`ai/scripts/materialize_saliency_features.py` appends saliency aggregates to an
existing AI feature table. It reads `.jsonl` or `.parquet`, resolves one source
clip per row, decodes a bounded sample through FFmpeg, runs the fork saliency
helper, and writes `saliency_mean` / `saliency_var` plus an optional status
column.

Use it before retraining predictor or MOS-head models when the current table
has placeholder or missing saliency columns.

## Usage

```bash
PYTHONPATH=. .venv/bin/python ai/scripts/materialize_saliency_features.py \
  --input runs/full_features_chug_hdr.jsonl \
  --output runs/full_features_chug_hdr_saliency.jsonl \
  --root .corpus/chug \
  --path-column src \
  --max-frames 8 \
  --frame-samples 8
```

The same command works for parquet by using `.parquet` input and output paths.
Parquet support uses the local pandas/pyarrow stack; JSONL only needs the
standard library plus the saliency runtime dependencies.

## Row Contract

Default input columns:

| Column | Meaning |
| --- | --- |
| `src` | Absolute source path, or a path relative to `--root`. |
| `width` | Source width in pixels. Missing or invalid values fall back to ffprobe. |
| `height` | Source height in pixels. Missing or invalid values fall back to ffprobe. |

Output columns:

| Column | Meaning |
| --- | --- |
| `saliency_mean` | Mean value of the returned saliency mask. |
| `saliency_var` | Variance of the returned saliency mask. |
| `saliency_status` | Row status. Disable with `--status-column ""`. |

Rows that already contain finite `saliency_mean` and `saliency_var` are skipped
unless `--overwrite` is set.

## Status Values

| Status | Meaning |
| --- | --- |
| `ok` | Row decoded and saliency aggregates were written. |
| `skipped-existing` | Existing finite saliency columns were preserved. |
| `missing-source` | The configured path column was empty or did not resolve to a file. |
| `missing-geometry` | Geometry was absent and ffprobe could not recover it. |
| `decode-failed` | FFmpeg did not produce the temporary raw `yuv420p` sample. |
| `model-failed` | The saliency helper raised an error. |

The process exits `0` when all rows are `ok` or `skipped-existing`; it exits
`1` when any row failed.

## Reproducer

```bash
PYTHONPATH=. .venv/bin/python -m pytest ai/tests/test_materialize_saliency_features.py -q
```
