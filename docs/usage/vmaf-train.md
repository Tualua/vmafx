# `vmaf-train` — tiny-AI training harness CLI

`vmaf-train` trains, exports, audits and registers the tiny ONNX models that
`vmaf` and `vmaf-tune` consume. It is the Python entry point of the fork's
tiny-AI training infrastructure (`ai/src/vmaf_train/cli.py`, registered in
`ai/pyproject.toml` `[project.scripts]`). This page covers all 15
subcommands. For what the models do, see
[`docs/ai/overview.md`](../ai/overview.md); for the training-corpus pipeline,
see [`docs/ai/training.md`](../ai/training.md).

## Install

```bash
pip install -e ai
vmaf-train --help
```

## Quick path

The pipeline runs in this order: extract features, fit (or tune), export, check
ops, validate normalisation, evaluate, register. The numbered recipe below
trains and registers a new `fr_regressor` model.

1. Pre-compute features for a dataset named in the manifest
   (`nflx`, `konvid-1k`, `live-vqc`, `youtube-ugc` or `bvi-dvc`):

    ```bash
    vmaf-train extract-features \
      --dataset nflx \
      --output .corpus/netflix/features.parquet
    ```

2. Train from a YAML config. `--output` is the run directory; the checkpoint is
   `<output>/last.ckpt`. Use `tune` instead for an Optuna sweep:

    ```bash
    vmaf-train fit \
      --config ai/configs/fr_tiny_v1.yaml \
      --cache .corpus/netflix/features.parquet \
      --output runs/fr_tiny_v1
    ```

3. Export to ONNX:

    ```bash
    vmaf-train export \
      --checkpoint runs/fr_tiny_v1/last.ckpt \
      --output runs/fr_tiny_v1/fr_tiny_v1.onnx \
      --model fr_regressor
    ```

4. Check the op allowlist:

    ```bash
    vmaf-train check-ops --model runs/fr_tiny_v1/fr_tiny_v1.onnx
    ```

5. Validate the normalisation against the corpus:

    ```bash
    vmaf-train validate-norm \
      --model runs/fr_tiny_v1/fr_tiny_v1.onnx \
      --features .corpus/netflix/features.parquet \
      --fail-on-warning
    ```

6. Evaluate on the test split:

    ```bash
    vmaf-train eval \
      --model runs/fr_tiny_v1/fr_tiny_v1.onnx \
      --features .corpus/netflix/features.parquet \
      --split test
    ```

7. Register the model (copy it under `model/tiny/` first):

    ```bash
    vmaf-train register \
      --model model/tiny/fr_tiny_v1.onnx \
      --kind fr \
      --dataset nflx \
      --license BSD-2-Clause-Patent \
      --train-commit "$(git rev-parse HEAD)" \
      --train-config ai/configs/fr_tiny_v1.yaml
    ```

To shrink a trained model, quantise it:

```bash
vmaf-train quantize-int8 \
  --fp32 model/tiny/fr_regressor_v1.onnx \
  --output model/tiny/fr_regressor_v1.int8.onnx \
  --calibration .corpus/netflix/features.parquet \
  --rmse-gate 0.5 \
  --json quantize-report.json
```

## Subcommand reference

### Train

#### `extract-features`

Pre-compute libvmaf features over a dataset's (ref, dis) pairs and write them
to a parquet cache.

| Flag | Purpose |
| --- | --- |
| `--dataset NAME` | Dataset name from the manifests (`nflx`, `konvid-1k`, `live-vqc`, `youtube-ugc`, `bvi-dvc`); required. |
| `--output PATH` | Parquet output path; required. |
| `--vmaf-binary PATH` | libvmaf CLI binary (default `core/build-cpu/tools/vmaf`). |

#### `manifest-scan`

Scan a local corpus directory for YUV, Y4M, MP4, MKV and WebM files, pin each
by SHA-256, and overwrite the in-tree manifest of the dataset.

| Flag | Purpose |
| --- | --- |
| `--dataset NAME` | Dataset name; required. |
| `--root PATH` | Local dataset root; required. |
| `--mos-csv PATH` | Optional CSV with columns `key,mos`. |

#### `fit`

Train a model from a YAML config. Configs live in `ai/configs/`
(`fr_tiny_v1.yaml`, `nr_mobilenet_v1.yaml`, `filter_residual_v1.yaml`,
`learned_filter_v1_qat.yaml`).

| Flag | Purpose |
| --- | --- |
| `--config PATH` | YAML config; required. |
| `--cache PATH` | Override the features cache (`.parquet` or `.npz`). |
| `--output PATH` | Override the output run directory. |
| `--epochs N` | Override the epoch count. |
| `--seed N` | Override the random seed. |

#### `tune`

Run an Optuna sweep over the `model_args` of a base YAML config.

| Flag | Purpose |
| --- | --- |
| `--config PATH` | Base YAML config; required. |
| `--param SPEC` (`-p`) | Repeatable search spec: `name=float:LOW:HIGH[:log]`, `name=int:LOW:HIGH` or `name=choice:A,B`. |
| `--trials N` | Number of Optuna trials (default 20). |
| `--study-name STR` | Study name (default `vmaf-train-sweep`). |
| `--storage URL` | Optional Optuna storage URL (`sqlite:///path`, ...). |
| `--cache PATH` | Override the features cache. |
| `--output PATH` | Override the sweep output root. |
| `--epochs N`, `--seed N` | Override epochs per trial, base seed. |

### Export and evaluate

#### `export`

Export a trained checkpoint to ONNX with a round-trip validation step.

| Flag | Purpose |
| --- | --- |
| `--checkpoint PATH` | Lightning checkpoint; required. |
| `--output PATH` | ONNX output; required. |
| `--model NAME` | `fr_regressor` (default), `nr_metric` or `learned_filter`. |
| `--opset N` | ONNX opset (default 17). |
| `--atol FLOAT` | Round-trip tolerance, torch vs onnxruntime (default `1e-05`). |

#### `eval`

Evaluate an ONNX model on a deterministic split and report PLCC, SROCC and
RMSE.

| Flag | Purpose |
| --- | --- |
| `--model PATH` | ONNX model; required. |
| `--features PATH` | Feature parquet; required. |
| `--split NAME` | `train`, `val` or `test` (default `test`). |
| `--input-name NAME` | ONNX input name (default `features`). |

#### `quantize-int8`

Post-training static int8 quantisation (QDQ format) per ADR-0173. It reports
drift against held-out samples and exits 2 if the RMSE breaks the gate.

| Flag | Purpose |
| --- | --- |
| `--fp32 PATH` | fp32 ONNX input; required. |
| `--output PATH` | int8 ONNX output; required. |
| `--calibration PATH` | Calibration parquet feature cache; required. |
| `--input-name NAME` | ONNX input name (default `features`). |
| `--n-calibration N` | Calibration sample count (default 512). |
| `--batch-size N` | Calibration batch size (default 32). |
| `--rmse-gate FLOAT` | Exit 2 if the int8-vs-fp32 RMSE exceeds this (default 1.0). |
| `--json PATH` | Write a JSON report. |

### Audit

All audit commands that take `--json` write the report to the given **path**
(they do not switch stdout to JSON).

#### `check-ops`

Check one ONNX model against libvmaf's op allowlist
(`core/src/dnn/op_allowlist.c`). Exits 2 if forbidden ops are found.

| Flag | Purpose |
| --- | --- |
| `--model PATH` | ONNX model; required. |

#### `audit-compat`

Audit every tiny model in a directory for feature-contract drift, the case
where libvmaf's feature columns changed but a shipped model expects the old
shape.

| Flag | Purpose |
| --- | --- |
| `--model-dir PATH` | Directory with `.onnx` models and sidecars (default `model/tiny`). |
| `--fail-on-warning` | Exit 2 if any audit issue is found. |

#### `validate-norm`

Compare a sidecar's declared feature normalisation against real data. It flags
features whose declared mean drifts more than 1 sigma from the observed mean, or
where more than 5% of samples lie more than 3 sigma from it. This catches silent
normalisation breakage that makes inference under-predict.

| Flag | Purpose |
| --- | --- |
| `--model PATH` | ONNX model or its `.json` sidecar; required. |
| `--features PATH` | Feature parquet; required. |
| `--fail-on-warning` | Exit 2 if any drift exceeds the threshold. |
| `--json PATH` | Write a JSON report. |

#### `profile`

Measure latency and peak-RSS delta per (provider, shape): mean, p50 and p99.
Use it to pick a deployment target or as a CI latency gate.

| Flag | Purpose |
| --- | --- |
| `--model PATH` | ONNX model; required. |
| `--shape N,C,H,W` | Repeatable input shape (default: the graph's shape). |
| `--provider NAME` | Repeatable ORT provider (default: all available). |
| `--warmup N` | Warmup iterations (default 5). |
| `--iters N` | Timed iterations (default 100). |
| `--json PATH` | Write a JSON report. |

#### `cross-backend`

Run a model on the CPU provider and every other available ORT provider and
diff the outputs, to catch provider-specific numerical drift.

| Flag | Purpose |
| --- | --- |
| `--model PATH` | ONNX model; required. |
| `--features PATH` | Feature parquet; without it a synthetic input is used. |
| `--provider NAME` | Repeatable ORT provider (default: every non-CPU provider available). |
| `--shape N,C,H,W` | Synthetic input shape (ignored with `--features`). |
| `--n-rows N` | Maximum rows from the parquet (default 256). |
| `--atol FLOAT` | Absolute-error threshold (default `0.001`). |
| `--fail-on-mismatch` | Exit 2 when a provider exceeds `--atol`. |
| `--json PATH` | Write a JSON report. |

#### `audit-learned-filter`

Pre-deploy audit of a `learned_filter` model. It runs the filter over a frame
corpus and flags four failures: mean shift, std inflation, clipping at codec
boundaries and SSIM collapse.

| Flag | Purpose |
| --- | --- |
| `--model PATH` | Filter ONNX; required. |
| `--frames PATH` | `.npy` file of shape `(N, H, W)` with values in `[0, peak]`; required. |
| `--peak FLOAT` | Maximum pixel value (default 1.0). |
| `--input-name NAME` | ONNX input name (default `input`). |
| `--ssim-min FLOAT` | Warn if per-frame SSIM(in, out) falls below this (default 0.6). |
| `--mean-shift-max FLOAT` | Mean-shift gate (default 0.05). |
| `--std-ratio-max FLOAT` | Std-ratio gate (default 2.0). |
| `--clip-fraction-max FLOAT` | Clipped-pixel fraction gate (default 0.01). |
| `--json PATH` | Write a JSON report. |
| `--fail-on-warning` | Exit 2 on any warning. |

#### `bisect-model-quality`

Binary-search an ordered list of checkpoints (head assumed good, tail assumed
bad) for the first one that violates a PLCC, SROCC or RMSE gate on a held-out
feature cache. If the list is not ordered good to bad, it exits 0 with the
verdict "no regression detected" or "first model already fails". Companion to
the `/bisect-model-quality` skill.

| Flag | Purpose |
| --- | --- |
| `models` | Positional ordered list of ONNX checkpoints; required. |
| `--features PATH` | Held-out feature parquet with a `mos` column; required. |
| `--min-plcc FLOAT`, `--min-srocc FLOAT` | Lower bounds. |
| `--max-rmse FLOAT` | Upper bound. |
| `--input-name NAME` | ONNX input tensor name (default `input`). |
| `--json PATH` | Write a JSON report. |
| `--fail-on-first-bad` | Exit 2 when a regression is localised. |

### Register

#### `register`

Write the sidecar metadata JSON (`<model>.json`) beside a shipped ONNX model
(ADR-0211).

| Flag | Purpose |
| --- | --- |
| `--model PATH` | ONNX model; required. |
| `--kind KIND` | `fr`, `nr` or `filter`; required. |
| `--dataset NAME` | Training dataset identifier. |
| `--license SPDX` | License SPDX identifier. |
| `--train-commit SHA` | Training commit. |
| `--train-config PATH` | Training-config path. |
| `--manifest PATH` | Optional supplementary manifest. |

## JSON report provenance

Every `vmaf-train` subcommand that writes a durable JSON report with `--json`
adds an ADR-0661 `run_provenance` block. It covers `validate-norm`, `profile`,
`audit-learned-filter`, `quantize-int8`, `cross-backend` and
`bisect-model-quality`.

The block records:

- `entrypoint`: `ai/src/vmaf_train/cli.py` plus a SHA-256 of the CLI file.
- `argv` and `args`: the invoked command arguments and parsed option values.
- `inputs`: the model, feature table, calibration table, frame corpus or model
  list behind the report.
- `outputs`: the JSON report path, plus generated model outputs where the
  command writes one, such as `quantize-int8 --output`.

Use the block when attaching reports to model cards, promotion PRs or
regression investigations: it is the reproducibility pointer to the exact files
and thresholds.

## Related

- [`vmaf-tune`](vmaf-tune.md) — encode automation that consumes the models
  produced here.
- [`vmaf`](cli.md) — scoring CLI (`--tiny-model` flag).
- [`docs/ai/training.md`](../ai/training.md) — training-corpus pipeline.
- [`docs/ai/quantization.md`](../ai/quantization.md) — post-training
  quantisation (ADR-0173).
