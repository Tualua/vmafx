<!-- markdownlint-disable MD013 -->
# Tiny AI — training data

This page documents the local layout of the Netflix VMAF training corpus, the
loader that reads it, and how to evaluate a model trained on it. For the
training workflow itself (feature extraction, fit, export, eval) see
[training.md](training.md).

## Corpus location

Training data is never committed. All YUV files are gitignored. The canonical
local path for the Netflix corpus is:

```text
.corpus/netflix/
  ref/    # 9 reference YUVs
  dis/    # 70 distorted YUVs
```

KoNViD-1k uses a separate local root, because the public dataset ships MP4 clips
rather than Netflix-style YUV reference and distorted pairs:

```text
$VMAF_KONVID_1K_DIR/
  KoNViD_1k_videos/    # 1200 source MP4s
```

`ai/scripts/konvid_to_full_features.py` also accepts `--konvid-root`. When
`VMAF_KONVID_1K_DIR` is unset it falls back to `$VMAF_DATA_ROOT/konvid-1k` and
then `~/datasets/konvid-1k`.

Dataset fetch helpers write run manifests beside the local data roots before
conversion starts:

- `ai/scripts/fetch_konvid_1k.py` writes `<root>/fetch_manifest.json` by default
  and accepts `--manifest-out`.
- `ai/scripts/fetch_youtube_ugc_subset.py` keeps its existing stem-to-files
  content manifest and writes a separate `<manifest>.run-manifest.json` sidecar
  unless `--run-manifest-out` is supplied.

Keep these gitignored sidecars with the downloaded corpora when citing training
evidence.

### Naming convention

The loader (`ai/data/netflix_loader.py`) expects these file names:

```text
ref/<source>_<fps>fps.yuv
dis/<source>_<quality>_<height>_<bitrate-kbps>.yuv
```

For example:

```text
ref/
  BigBuckBunny_25fps.yuv        # one reference per source
dis/
  BigBuckBunny_1_576_2000.yuv   # source, quality 1, 576-line ladder rung, 2000 kbps
```

`<quality>` is an opaque integer assigned at encode time. All corpus YUVs are
1920x1080 `yuv420p` 8-bit regardless of the `<height>` token, which records the
encode-ladder rung before upscale to the reference resolution. The loader checks
the file size against that assumption and falls back to ffprobe when it does not
match.

## Loader

Two loaders read the corpus:

| Entry point | Used by | Reads |
| --- | --- | --- |
| `ai/train/dataset.py::NetflixFrameDataset` | `ai/train/train.py`, `ai/train/train_combined.py`, `--data-root` | `<data-root>/ref/` and `<data-root>/dis/` YUVs |
| `vmaf-train` datasets and manifests (`ai/src/vmaf_train/data/datasets.py`) | `vmaf-train extract-features --dataset nflx`, `manifest-scan` | A SHA-256 manifest of a dataset under `$VMAF_DATA_ROOT` (default `~/.cache/vmaf-train`) |

`NetflixFrameDataset` works as follows:

1. Scan `<data-root>/ref/` and `<data-root>/dis/` for `.yuv` files and pair each
   distorted file with its reference by the `<source>` token. A distorted file
   without a reference is skipped.
2. Run libvmaf through a subprocess to extract the six-element feature vector
   per frame, and the teacher score per frame.
3. Cache the per-clip features and scores as JSON under
   `~/.cache/vmaf-tiny-ai/<source>/<dis_basename>.json`. Override the cache root
   with `VMAF_TINY_AI_CACHE`. A cache entry whose stamped teacher differs from
   the resolved teacher is recomputed, see
   [training.md](training.md#teacher-model-provenance).

Each sample is one frame: a feature vector paired with the teacher per-frame
score in `[0, 100]`.

`--data-root` exists on `ai/train/train.py` and `train_combined.py`
(`--netflix-root`). The `vmaf-train` subcommands take no `--data-root`.
`VMAF_DATA_ROOT` is the dataset cache root of the `vmaf-train` manifests, not a
substitute for `--data-root`.

### Use the `vmaf-train` manifest path

1. Scan the corpus into the `nflx` manifest:

    ```bash
    vmaf-train manifest-scan --dataset nflx --root $VMAF_DATA_ROOT/nflx
    ```

2. Extract features:

    ```bash
    vmaf-train extract-features \
        --dataset nflx \
        --vmaf-binary core/build-cpu/tools/vmaf \
        --output ai/data/nflx_features.parquet
    ```

## Evaluation harness

After extraction, fit and evaluate a model against the teacher soft labels. The
teacher is resolved from the ADR-1168 single source `DEFAULT_MODEL`, or set with
`--assume-teacher` for legacy datasets (ADR-1173).

1. Train:

    ```bash
    vmaf-train fit \
        --config ai/configs/fr_tiny_v1.yaml \
        --cache ai/data/nflx_features.parquet \
        --output runs/fr_tiny_nflx/
    ```

2. Export:

    ```bash
    vmaf-train export \
        --checkpoint runs/fr_tiny_nflx/last.ckpt \
        --output runs/fr_tiny_nflx/fr_tiny_nflx.onnx \
        --opset 17
    ```

3. Evaluate on the held-out test split. The command reports PLCC, SROCC and RMSE
   against the labels in the feature table:

    ```bash
    vmaf-train eval \
        --model runs/fr_tiny_nflx/fr_tiny_nflx.onnx \
        --features ai/data/nflx_features.parquet \
        --split test
    ```

4. Run the MCP server health check (ADR-0242):

    ```bash
    cd mcp-server/vmaf-mcp && python -m pytest tests/test_smoke_e2e.py -v
    ```

## Data path safety invariants

- **Never commit YUV files.** The `.gitignore` at the repo root lists `*.yuv`
  and
  `.corpus/`. Do not override these entries.
- The training script takes `--data-root` as an explicit CLI flag to avoid
  hard-coding the local path. CI does not have the corpus; the smoke test in
  `test_smoke_e2e.py` uses only the committed Netflix golden fixture
  (`python/test/resource/yuv/src01_hrc00_576x324.yuv`), not the training corpus.

## Split reproducibility

`ai/train/` holds out one source for validation: `--val-source`, default
`Tennis`. The split is deterministic, because the hold-out source name is the
only knob (ADR-0203). It is content-disjoint: with 9 sources the hold-out gives
about 88 % train and 12 % validation by clip count. A random frame split would
leak frames of one clip into both halves and inflate PLCC by 5 to 10 percentage
points. For leave-one-source-out over all 9 sources see
[loso-eval.md](loso-eval.md). `vmaf-train eval` splits by a deterministic hash of
the clip key (fixed salt `vmaf-train-splits-v1`), so the same clip always lands
in the same split, as described in
[training.md](training.md#determinism).

## See also

- [training.md](training.md) — full training workflow
- [inference.md](inference.md) — running the trained ONNX model via C API or CLI
- [ADR-0242](../adr/0242-tiny-ai-netflix-training-corpus.md) — architecture
  and distillation policy decisions
- [ADR-0417](../adr/0417-tiny-ai-netflix-training-scaffold-pr.md) — draft PR
  registration; consult before triggering a training run
- [Research digest 0019](../research/0019-tiny-ai-netflix-training.md) —
  VMAF methodology survey and distillation literature (2026-04-27)
- [Research digest 0099](../research/0099-tiny-ai-netflix-training-update.md) —
  2024–2026 distillation, ONNX Runtime, and lightweight FR regressor update
- [Research digest
  0612](../research/0612-tiny-ai-netflix-training-scaffold-2026-05-19.md) —
  2024–2026 refresh: EfficientVMAF, temperature-scaled distillation, ORT
  1.19/1.20,
  feature-reweighting (2026-05-19)
- [ADR-0612](../adr/0612-tiny-ai-netflix-training-scaffold-2026-05-19.md) —
  architecture
  alternatives table and training-data contract formalised (2026-05-19 scaffold
  iteration)
- [ADR-0640](../adr/0640-tiny-ai-netflix-training-scaffold-2026-05-20.md) —
  EfficientVMAF
  survey update, feature-reweighting alternative added (2026-05-20 scaffold
  iteration)
- [Research digest
  0615](../research/0615-tiny-ai-netflix-training-2026-05-20.md) —
  EfficientVMAF (CVPR 2024), IQA-PyTorch distillation, ORT 1.20 update
  (2026-05-20)
