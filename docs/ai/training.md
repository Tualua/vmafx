<!-- markdownlint-disable MD060 -->
# Tiny AI — training

Train, export and register a tiny ONNX model with `vmaf-train`, the typer CLI
in [`ai/`](../../ai/). This page covers the training recipes. The full
subcommand reference is [`vmaf-train`](../usage/vmaf-train.md). What a tiny
model is and how to run one is in [overview.md](overview.md) and
[inference.md](inference.md).

!!! note
    The models shipped today were trained against the `vmaf_v0.6.1` teacher.
    The one-shot retrain against the `vmaf_v1.0.16_3d0h` teacher is RC9
    work, see the [roadmap](../roadmap.md) and
    [the retrain runbook](retrain-runbook-1246.md).

## Pick a track

| Track | Model | Config | Input | Output |
| --- | --- | --- | --- | --- |
| C1 | Full-reference regressor on libvmaf features | [`fr_tiny_v1.yaml`](../../ai/configs/fr_tiny_v1.yaml) | 6 canonical features per frame | score in 0..100 |
| C2 | No-reference metric | [`nr_mobilenet_v1.yaml`](../../ai/configs/nr_mobilenet_v1.yaml) | decoded frames | score |
| C3 | Learned residual filter | [`filter_residual_v1.yaml`](../../ai/configs/filter_residual_v1.yaml) | BVI-DVC encoder-distortion pairs | filtered frame |

## Install

```bash
pip install -e ai
# optional extras
pip install -e 'ai[tune,viz]'
```

This pulls `torch>=2.14.1,<3.0` and `pytorch-lightning>=2.6.6,<3.0` (the
`lightning` PyPI package was renamed to `pytorch-lightning` on 2026-04-30).
If you have a GPU-capable PyTorch wheel installed separately, the extras do not
reinstall it.

## Commands

`vmaf-train` has 15 subcommands: `extract-features`, `fit`, `tune`, `export`,
`eval`, `manifest-scan`, `validate-norm`, `profile`, `audit-compat`,
`check-ops`, `audit-learned-filter`, `quantize-int8`, `cross-backend`,
`bisect-model-quality` and `register`. Flags and examples for each are in
[`vmaf-train`](../usage/vmaf-train.md). The training flow uses five of them:

| Step | Command | Purpose |
| --- | --- | --- |
| 1 | `extract-features` | dump per-frame libvmaf features to parquet |
| 2 | `fit` | train from a YAML config |
| 3 | `export` | export a checkpoint to ONNX with a roundtrip check |
| 4 | `eval` | PLCC, SROCC and RMSE on a held-out split |
| 5 | `register` | write the sidecar metadata JSON |

## Dataset acquisition

Datasets are not committed. `ai/src/vmaf_train/data/datasets.py` knows five
canonical sources and caches them under
`${VMAF_DATA_ROOT:-~/.cache/vmaf-train}/datasets/<name>/`. Each dataset ships a
`manifests/<name>.yaml` SHA-256 manifest so downloads are verifiable. The
shipped manifests are empty: after fetching a dataset, regenerate its manifest
with `vmaf-train manifest-scan --dataset <name> --root <dir>`.

| Dataset | Use | License | Purpose |
| --- | --- | --- | --- |
| Netflix Public (NFLX) | C1, C2 | Netflix research | Same source as upstream `vmaf_v0.6.1` |
| KoNViD-1k | C2 | CC BY 4.0 | NR-friendly UGC clips with MOS |
| LIVE-VQC | C2 | Academic | NR validation |
| YouTube-UGC | C2 | CC BY 3.0 | Large-scale NR |
| BVI-DVC | C3 | Academic | Encoder distortion pairs for learned filters |

You are responsible for complying with each dataset's license. The manifests
record hashes, not bytes. The larger MOS corpora (KonViD-150k, LSVQ, CHUG,
Waterloo IVC and others) are covered in [mos-corpora.md](mos-corpora.md), and
the local Netflix layout in [training-data.md](training-data.md).

## C1 FR regressor

C1 has three entry points that share one model factory and one ONNX output
layout. Use the parquet flow for the `vmaf-train` CLI, the Netflix corpus flow
for the runnable `ai/train/` pipeline, and the KoNViD flow to add a second
corpus.

### Parquet flow

1. Extract feature vectors from the dataset pairs with the libvmaf CPU backend.
   The `nflx` manifest must exist first (see Dataset acquisition).

    ```bash
    vmaf-train extract-features \
        --dataset nflx \
        --vmaf-binary core/build-cpu/tools/vmaf \
        --output ai/data/nflx_features.parquet
    ```

2. Train a 2-layer MLP on the extracted features. `--cache` overrides the
   `cache:` path in the config; `fit` has no `--features` option.

    ```bash
    vmaf-train fit \
        --config ai/configs/fr_tiny_v1.yaml \
        --cache ai/data/nflx_features.parquet \
        --output runs/fr_tiny_v1/
    ```

3. Export the trained weights to ONNX and validate the roundtrip (torch eval
   against onnxruntime, default `--atol 1e-5`). `--model` selects the family
   (`fr_regressor`, `nr_metric` or `learned_filter`).

    ```bash
    vmaf-train export \
        --checkpoint runs/fr_tiny_v1/last.ckpt \
        --model fr_regressor \
        --output runs/fr_tiny_v1/fr_tiny_v1.onnx \
        --opset 17
    ```

4. Evaluate on the held-out split. The command prints PLCC, SROCC and RMSE
   against MOS.

    ```bash
    vmaf-train eval \
        --model runs/fr_tiny_v1/fr_tiny_v1.onnx \
        --features ai/data/nflx_features.parquet \
        --split test
    ```

5. Write the sidecar next to the ONNX file. Use the license of your own
   training data and code; the shipped models in
   [`model/tiny/registry.json`](../../model/tiny/registry.json) carry
   `BSD-2-Clause-Patent`, `BSD-2-Clause` or `MIT`.

    ```bash
    vmaf-train register \
        --model runs/fr_tiny_v1/fr_tiny_v1.onnx \
        --kind fr \
        --dataset nflx \
        --license BSD-2-Clause-Patent \
        --train-commit "$(git rev-parse HEAD)"
    ```

`register` writes `<model>.json` with this metadata
(`ai/src/vmaf_train/registry.py::ModelMetadata`; unknown keys are rejected):

```json
{
  "schema_version": 1,
  "name": "fr_tiny_v1",
  "kind": "fr",
  "onnx_opset": 17,
  "input_names": ["features"],
  "output_names": ["score"],
  "normalization": { "mean": [], "std": [] },
  "expected_output_range": [0.0, 100.0],
  "dataset": "nflx",
  "train_commit": "…",
  "train_config_hash": "sha256:…",
  "license": "BSD-2-Clause-Patent"
}
```

`parent_dataset_manifest`, `cosign_signature` (filled in by the release
workflow) and `notes` are optional. `train_config_hash` is computed when
`--train-config` is passed.

!!! note
    Sidecars of models shipped under `model/tiny/` come from the dedicated
    exporters and use a flatter layout (`id`, `input_name`, `output_name`,
    `input_mean`, `input_std`, `onnx_has_scaler`, ...). See
    [model-registry.md](model-registry.md).

### Netflix corpus flow

Once the local Netflix corpus exists at `.corpus/netflix/` (layout in
[training-data.md](training-data.md), scope in ADR-0242), the prep stack under
[`ai/data/`](../../ai/data/) and [`ai/train/`](../../ai/train/) gives a
runnable end-to-end pipeline instead of the parquet flow. ADR-0203 records the
decisions: distillation source, val-split policy, architecture roster and
cache layout.

1. Build libvmaf once, so the cache warm-up can call it.

    ```bash
    meson setup build core -Denable_cuda=false -Denable_sycl=false
    ninja -C build
    ```

2. Train. The first run pre-warms the per-clip cache at `$VMAF_TINY_AI_CACHE`
   (default `~/.cache/vmaf-tiny-ai`); later runs only re-train. The defaults
   are `--model-arch mlp_small`, `--val-source Tennis` and `--epochs 10`.

    ```bash
    python ai/train/train.py \
        --data-root .corpus/netflix \
        --model-arch mlp_small \
        --epochs 30 \
        --batch-size 256 \
        --lr 1e-3 \
        --out-dir runs/tiny_nflx
    ```

    The wrapper `bash ai/scripts/run_training.sh` is equivalent.

3. Evaluate the final checkpoint on the validation split.

    ```bash
    python -c "
    from pathlib import Path
    import numpy as np
    from ai.train.dataset import NetflixFrameDataset
    from ai.train.eval import evaluate

    val = NetflixFrameDataset(Path('.corpus/netflix'), split='val')
    X, y = val.numpy_arrays()
    report = evaluate(
        features=X,
        targets=y,
        onnx_path=Path('runs/tiny_nflx/mlp_small_final.onnx'),
        out_path=Path('runs/tiny_nflx/eval_report.json'),
    )
    print(report)
    "
    ```

    The JSON report contains `n_samples`, `plcc`, `srocc`, `krocc`, `rmse`,
    `latency_ms_p50_per_clip`, `latency_ms_p95_per_clip`, `model` and
    `feature_dim`. Latency is measured against a synthetic 240-frame clip on
    the CPU EP, because the point of a tiny model is to be meaningfully faster
    than the SVR.

#### Flags of `ai/train/train.py`

| Flag | Default | Notes |
|---|---|---|
| `--data-root` | `.corpus/netflix` | Directory with `ref/` and `dis/`. |
| `--model-arch` | `mlp_small` | One of `linear`, `mlp_small`, `mlp_medium`. |
| `--epochs` | 10 | `0` runs the smoke-export path and exits. |
| `--batch-size` | 256 | SGD batch size. |
| `--lr` | 1e-3 | Adam learning rate. |
| `--out-dir` | `runs/tiny_nflx` | ONNX checkpoints land at `<out-dir>/<arch>_epoch<n>.onnx` and `<arch>_final.onnx`. |
| `--val-source` | `Tennis` | Source name held out for validation. |
| `--max-pairs` | unset | Cap on (ref, dis) pairs (smoke / debugging). |
| `--no-export-onnx` | unset | Skip per-epoch ONNX dump (final still written). |
| `--assume-dims WxH` | unset | For tests / mock corpora with non-1080p YUVs. |

#### Architectures

| Arch | Layers | Params (feature_dim=6) |
|---|---|---|
| `linear` | `Linear(6, 1)` | 7 |
| `mlp_small` | `Linear(6,16) -> ReLU -> Linear(16,8) -> ReLU -> Linear(8,1)` | 257 |
| `mlp_medium` | `Linear(6,64) -> ReLU -> Linear(64,32) -> ReLU -> Linear(32,1)` | 2 561 |

#### Expected runtime

Indicative timings, not measured by CI:

| Phase | CPU-only (8-core) | CUDA (RTX 3060) |
|---|---|---|
| Cache warm (full corpus, 70 pairs) | 30–60 min (libvmaf-bound) | 5–8 min (libvmaf CUDA backend) |
| Train 30 epochs `mlp_small` | 1–2 min | <30 s |
| Train 30 epochs `mlp_medium` | 2–4 min | <60 s |
| ONNX export | <1 s | <1 s |

The cache is the bottleneck on the first run. Later runs re-use the JSON cache
and skip libvmaf. To force a re-extract, delete `$VMAF_TINY_AI_CACHE`.

#### Smoke command

CI runs only the `--epochs 0` smoke test, because the real corpus and a real
training run do not fit a GitHub runner. The test is
`ai/tests/test_train_smoke.py`; the equivalent shell command exports an
initial-weights ONNX without touching the real corpus or invoking libvmaf, and
is the documented reproducer in the PR template:

```bash
python ai/train/train.py \
    --epochs 0 \
    --data-root /tmp/mock_corpus \
    --assume-dims 16x16 \
    --val-source BetaSrc \
    --out-dir /tmp/tiny_smoke
```

### KoNViD-1k synthetic-distortion pairs

The 9-source Netflix Public corpus is fully used by the LOSO sweep.
Research-0023 section 5 documents how the FoxBird outlier reflects
content-distribution variance within those 9 clips. A different or larger
training corpus reduces that variance. KoNViD-1k (the Konstanz natural video
database: 1 200 user-generated clips at 540p with crowd-sourced MOS) is the
natural starting point, available at `$VMAF_DATA_ROOT/konvid-1k/` or
`$VMAF_KONVID_1K_DIR` after `ai/scripts/fetch_konvid_1k.py`.

KoNViD-1k is no-reference (clip plus MOS), not (ref, dis) pairs. To produce the
FR pairs the LOSO trainer expects, the acquisition step synthesises a distorted
variant per clip with a libx264 CRF=35 round trip, the recipe used for the
Netflix dis-pairs. It then runs libvmaf to extract the 6 canonical features and
the per-frame VMAF teacher score for each pair (the teacher is resolved from a
single source, ADR-1168 and ADR-1173).

#### Acquisition

1. Smoke run (5 clips, about 30 s wall):

    ```bash
    python ai/scripts/konvid_to_vmaf_pairs.py --max-clips 5
    ```

2. Full run (1 200 clips, about 30 min wall on the ryzen-4090 profile):

    ```bash
    python ai/scripts/konvid_to_vmaf_pairs.py
    ```

Output is `ai/data/konvid_vmaf_pairs.parquet` (gitignored). The schema matches
`NetflixFrameDataset.numpy_arrays()`:
`(key, frame_index, vif_scale0..3, adm2, motion2, vmaf, teacher_model)` per row.
The command also writes `ai/data/konvid_vmaf_pairs.manifest.json` by default,
with CRF, feature names, clip and frame counts, failed clip IDs, the VMAF
binary and model inputs, and `run_provenance`. Pass `--manifest-out PATH` when
the parquet lives under a different experiment directory. Per-clip JSON caches
live under `$VMAF_TINY_AI_CACHE/konvid-1k/<key>.json`, so re-runs are
idempotent and only newly added clips re-extract.

#### Full-feature refresh

For the current full-feature FR refresh, use the fork CPU `vmaf` binary
explicitly:

```bash
# smoke
python ai/scripts/konvid_to_full_features.py \
    --konvid-root "$VMAF_KONVID_1K_DIR" \
    --vmaf-bin core/build-cpu/tools/vmaf \
    --max-clips 5

# full run
python ai/scripts/konvid_to_full_features.py \
    --konvid-root "$VMAF_KONVID_1K_DIR" \
    --vmaf-bin core/build-cpu/tools/vmaf
```

This writes `runs/full_features_konvid.parquet` plus
`runs/full_features_konvid_with_folds.parquet`. The folded file adds
`source=fold0..fold4` using a deterministic balanced hash order over clip keys,
so `eval_multiseed_v3_v4.py` can reproduce the KoNViD 5-fold gate without stale
local parquet files.

The script also writes `runs/full_features_konvid.manifest.json` by default. It
records the KoNViD root, resolved videos directory, cache directory, vmaf
binary, model path, CRF/codec recipe, fold settings, selected and processed
clip counts, row and column counts, and the ADR-0661 `run_provenance` block.
Pass `--manifest-out PATH` to keep the sidecar inside a dated experiment
bundle.

#### Combine refreshed FULL_FEATURES shards

After the Netflix, KoNViD, BVI-DVC and optional UGC refreshes finish, rebuild
aggregate training tables with the combiner instead of manual
`pandas.concat`:

```bash
python ai/scripts/combine_full_feature_parquets.py \
    --input netflix=runs/full_features_netflix_refresh_20260520.parquet \
    --input konvid=runs/full_features_konvid_refresh_20260520.parquet \
    --input bvi=runs/full_features_bvi_dvc_D_refresh_20260520.parquet \
    --out runs/full_features_4corpus_refresh_20260520.parquet

python ai/scripts/combine_full_feature_parquets.py \
    --input base=runs/full_features_4corpus_refresh_20260520.parquet \
    --input ugc=runs/full_features_ugc_refresh_20260520.parquet \
    --out runs/full_features_5corpus_refresh_20260520.parquet
```

The combiner normalizes every input to
`corpus, source, frame_index, codec, teacher_model, <FULL_FEATURES>, vmaf`,
fills missing feature columns with `NaN`, and preserves the caller-provided
corpus label. It also writes `<out>.manifest.json` by default, with per-input
row counts, missing-feature fill lists, output column order, the aggregate
corpus distribution and `run_provenance`. Pass `--manifest-out PATH` only when
the manifest must live next to a separate experiment bundle.

The standalone KoNViD and BVI-DVC full-feature builders follow the same sidecar
rule (`runs/full_features_konvid.manifest.json` and
`runs/full_features_bvi_dvc_<tier>.manifest.json` by default), so each
refreshed shard can be replayed before it is combined.

#### Teacher model provenance

Every producer distils from one teacher: the fork default model
(`vmaf_v1.0.16_3d0h`, single-sourced from `core/include/libvmaf/model.h` and
`vmaftune.defaultmodel.DEFAULT_MODEL`, ADR-1168; ADR-1173). The teacher is
resolved in this order by `ai.data.scores.resolve_teacher_model()`:

1. an explicit override: `--vmaf-model` (`extract_full_features.py`,
   `extract_k150k_features.py`) or `--model` (`bvi_dvc_to_full_features.py`,
   `extract_ugc_features.py`, `konvid_to_full_features.py`,
   `konvid_to_vmaf_pairs.py`, `bvi_dvc_to_corpus_jsonl.py`). It accepts a
   version name (`vmaf_v0.6.1`), a `version=` or `path=` libvmaf model spec, or
   a model JSON path;
2. `$VMAF_MODEL_PATH` (a model JSON file);
3. the single-source default.

Every feature row and run manifest carries a `teacher_model` column naming the
teacher that produced its `vmaf` target. Consumers refuse to mix teachers:

- `combine_full_feature_parquets.py`, `train_vmaf_tiny_v5.py` and
  `eval_loso_vmaf_tiny_v5.py` raise `ValueError` when a shard contains more
  than one `teacher_model`, when two inputs disagree, or when an input has no
  `teacher_model` column. Legacy tables (extracted before the column existed,
  that is with `vmaf_v0.6.1`) are accepted only with an explicit
  `--assume-teacher vmaf_v0.6.1`, which stamps that value on every row. The
  flag must match any stamp already present.
- The `NetflixFrameDataset` per-clip cache (`$VMAF_TINY_AI_CACHE`, default
  `~/.cache/vmaf-tiny-ai/`) is revalidated on read. An entry whose stamped
  teacher differs from the resolved teacher, or a legacy entry with no stamp,
  is a miss and is recomputed (logged at INFO), never relabelled. Expect one
  full re-extraction the first time a pre-ADR-1173 cache is reused.

Mixed-teacher tables are never merged silently, and no flag overrides a genuine
conflict.

#### Loader

[`ai/train/konvid_pair_dataset.py::KoNViDPairDataset`](../../ai/train/konvid_pair_dataset.py)
mirrors the interface of `NetflixFrameDataset`: the same `feature_dim` (6) and
the same `numpy_arrays() → (X, y)` shape, so the existing `_train_loop`
consumes it without modification.

```python
from ai.train.konvid_pair_dataset import KoNViDPairDataset

# all 1 200 clips
ds = KoNViDPairDataset("ai/data/konvid_vmaf_pairs.parquet")

# LOSO-style holdout: 1 clip val, rest train
val_keys = {ds.unique_keys[0]}
train_keys = set(ds.unique_keys) - val_keys
val_ds = KoNViDPairDataset("ai/data/konvid_vmaf_pairs.parquet", keep_keys=val_keys)
train_ds = KoNViDPairDataset("ai/data/konvid_vmaf_pairs.parquet", keep_keys=train_keys)

X, y = train_ds.numpy_arrays()  # (n_train_frames, 6), (n_train_frames,)
```

#### Combine KoNViD with the Netflix corpus

[`ai/train/train_combined.py`](../../ai/train/train_combined.py) concatenates
the Netflix `NetflixFrameDataset` train slice with the KoNViD
`KoNViDPairDataset` train slice on the feature axis. It feeds the union to the
same `_build_model`, `_train_loop` and `export_onnx` pipeline that
`ai/train/train.py` uses, so the model factory and ONNX layout stay identical.

```bash
# Default: hold out the Netflix Tennis source for val; KoNViD is
# fully in training. Mirrors the canonical ADR-0203 split so the
# result is directly comparable to mlp_small / mlp_medium baselines.
python ai/train/train_combined.py \
    --netflix-root .corpus/netflix \
    --konvid-parquet ai/data/konvid_vmaf_pairs.parquet \
    --model-arch mlp_small \
    --epochs 30 \
    --out-dir runs/tiny_combined
```

`--val-mode` selects the validation split:

| Mode                                | Validation set                              |
| ----------------------------------- | ------------------------------------------- |
| `netflix-source` (default)          | Netflix `--val-source` (default `Tennis`)   |
| `konvid-holdout`                    | Deterministic 10 % of KoNViD clip keys      |
| `netflix-source-and-konvid-holdout` | Union of the two above                      |
| `netflix-only`                      | KoNViD slice is omitted entirely            |
| `konvid-only`                       | Netflix slice is omitted entirely           |

KoNViD train/val splits hold out whole clips, not random frames, keyed off
`--seed` and `--konvid-val-fraction`, so frames from one clip cannot leak across
the split. ONNX checkpoints land at `<out-dir>/<arch>_combined_epoch<n>.onnx`
and `<arch>_combined_final.onnx`.

When the parquet is missing, the trainer prints a warning and falls back to the
Netflix-only path. When both corpora are missing it exports an initial-weights
ONNX and exits 0, so the smoke command still produces a deterministic artefact.

## MOS label materialization

Real MOS-head training expects feature tables that already carry `mos` or
`mos_raw_0_100`. If an extraction pass produced only metric columns, join the
subjective labels first with `ai/scripts/materialize_mos_labels.py`. The
command, its flags and the batch runner live in
[mos-label-materializer.md](mos-label-materializer.md).

The KonViD MOS trainer rejects real-path inputs that yield zero labelled rows
and writes no checkpoint. Use `--smoke` when the input is synthetic.

## C2 NR metric

C2 uses the same flow with a different config,
[`ai/configs/nr_mobilenet_v1.yaml`](../../ai/configs/nr_mobilenet_v1.yaml).
`extract-features` is replaced by a direct frame loader
([`frame_loader.py`](../../ai/src/vmaf_train/data/frame_loader.py)) that feeds
ffmpeg-decoded tensors into training.

The loader accepts these pixel formats:

- single-channel `gray`, as `HxW` arrays;
- packed colour formats `rgb24`, `bgr24`, `rgba` and `bgra`, as `HxWxC` arrays.

Other FFmpeg pixel formats fail before the decoder is spawned, so training jobs
never reinterpret planar or subsampled layouts as packed tensors.

## C3 learned filter

[`ai/configs/filter_residual_v1.yaml`](../../ai/configs/filter_residual_v1.yaml)
trains a residual CNN where the model is clamped to `x + residual` in
normalized space. The target is BVI-DVC encoder-distortion pairs.

## Determinism

`vmaf-train fit` seeds Python, NumPy and PyTorch with the config's `seed` field
and sets Lightning's `deterministic=True`. Given the same `train_commit`,
`train_config_hash`, `dataset_manifest_sha` and `seed`, the output weights are
reproducible to within float-rounding nondeterminism. CI flags a regression
when that difference exceeds a tight `allclose`.

## Hyperparameter sweeps

The `ai[tune]` extra pulls in Optuna and Ray Tune. `vmaf-train tune` wraps the
Optuna sweep helper around a base YAML config and searches `model_args`
entries. Each trial writes to `<output>/trial_NNN`, and the objective minimises
the best validation loss (`val/mse` for regressors, `val/l1` for learned
filters) recorded by Lightning.

```bash
pip install -e 'ai[tune]'
vmaf-train tune \
  --config ai/configs/fr_tiny_v1.yaml \
  --output runs/fr_tiny_sweep \
  --trials 20 \
  --param hidden=choice:16,32,64 \
  --param lr=float:0.0001:0.01:log
```

`--param` is repeatable and accepts three forms:

| Form | Example | Trial API |
| --- | --- | --- |
| `name=float:LOW:HIGH[:log]` | `lr=float:0.0001:0.01:log` | `trial.suggest_float` |
| `name=int:LOW:HIGH` | `depth=int:1:4` | `trial.suggest_int` |
| `name=choice:A,B,...` | `hidden=choice:16,32,64` | `trial.suggest_categorical` |

Values from `choice` are coerced to `int`, `float` or boolean when possible,
otherwise they stay strings. Use `--storage sqlite:///...` to resume or share an
Optuna study.

## Authoring training scripts

Scripts under `ai/scripts/` use the shared `aiutils` helpers and write a
`run_provenance` block into every durable report. The block schema, the helper
layer, the script bootstrap and the per-script report table are in
[run-provenance.md](run-provenance.md).

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| `extract-features` is slow | libvmaf CPU-only | rebuild with `-Denable_cuda=true` and rerun |
| `fit` OOM | batch size too big for GPU | edit `ai/configs/*.yaml` `batch_size`, or drop `precision` to `16-mixed` |
| Export roundtrip fails atol=1e-5 | op using `float16` with a value near `inf` | retrain in `float32` end-to-end, or tighten clamping |
