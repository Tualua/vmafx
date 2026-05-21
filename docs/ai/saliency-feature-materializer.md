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
  --model-id saliency_student_v2 \
  --temporal-aggregator ema \
  --ema-alpha 0.6 \
  --max-frames 8 \
  --frame-samples 8 \
  --audit-json runs/full_features_chug_hdr_saliency.audit.json
```

The same command works for parquet by using `.parquet` input and output paths.
Parquet support uses the local pandas/pyarrow stack; JSONL only needs the
standard library plus the saliency runtime dependencies.

`--temporal-aggregator` matches the `vmaf-tune` saliency reducers:
`mean`, `ema`, `max`, or `motion-weighted`. Use `mean` for the historical
clip average, `ema` when later frames should dominate but earlier frames still
matter, `max` when any salient frame should mark the clip, and
`motion-weighted` for a cheap video-saliency proxy that weights changing frames
more heavily. `--ema-alpha` controls the current-frame weight for `ema`.

`--model-id` records the model identity used for the run. It defaults to
`saliency_student_v1` when `--model-path` is omitted, or to the model-path stem
when a custom ONNX is supplied. Pass explicit ids such as
`saliency_student_v2` or `u2netp_mirror_v1` when comparing model families.

`--audit-json` writes row counters, the effective materializer config, and
ADR-0661 `run_provenance` for the input table, optional root/model path, output
table target, and audit target. Use it for any saliency-enriched table that
feeds retraining or signal-mix comparisons.

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
| `saliency_model_id` | Model id recorded for rows materialized in this run. Disable with `--model-id-column ""`. |
| `saliency_aggregator` | Temporal reducer used for rows materialized in this run. Disable with `--aggregator-column ""`. |
| `saliency_ema_alpha` | EMA alpha recorded for rows materialized in this run. Disable with `--ema-alpha-column ""`. |

Rows that already contain finite `saliency_mean` and `saliency_var` are skipped
unless `--overwrite` is set. Skipped rows keep their existing metadata; the
materializer does not invent a model id for older saliency columns whose origin
is unknown.

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
