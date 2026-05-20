# CHUG UGC-HDR ingestion

`ai/scripts/chug_to_corpus_jsonl.py` ingests the CHUG UGC-HDR dataset
into the fork's MOS-corpus JSONL shape.

## What CHUG Is

CHUG is the Crowdsourced User-Generated HDR Video Quality Dataset:
5,992 bitrate-ladder HDR videos derived from 856 UGC-HDR references,
with AMT subjective ratings and portrait/landscape coverage.

Repository: <https://github.com/shreshthsaini/CHUG>
Paper DOI: <https://doi.org/10.1109/ICIP55913.2025.11084488>

## License Posture

The CHUG README badge says CC BY-NC 4.0, while `license.txt` contains
Creative Commons Attribution-NonCommercial-ShareAlike 4.0 text. Treat
CHUG as non-commercial/share-alike research data until that mismatch is
clarified. Do not commit CHUG CSVs, videos, JSONL rows, feature caches,
or local trained heads.

## Quick Start

```bash
mkdir -p .workingdir2/chug
curl -L https://raw.githubusercontent.com/shreshthsaini/CHUG/master/chug.csv \
  -o .workingdir2/chug/manifest.csv
curl -L https://raw.githubusercontent.com/shreshthsaini/CHUG/master/chug-video.txt \
  -o .workingdir2/chug/chug-video.txt

PYTHONPATH=ai/src python ai/scripts/chug_to_corpus_jsonl.py --chug-dir .workingdir2/chug
```

The default run caps at `--max-rows 500`. Use `--full` for all 5,992
manifest rows:

```bash
PYTHONPATH=ai/src python ai/scripts/chug_to_corpus_jsonl.py \
  --chug-dir .workingdir2/chug \
  --output .workingdir2/chug/chug.jsonl \
  --full \
  --verbose
```

The run is resumable through `.workingdir2/chug/.download-progress.json`.
The adapter downloads each MP4 via `curl`, probes it with `ffprobe`,
deduplicates by SHA-256, and appends JSONL rows.

## Output Schema

The common MOS-corpus fields match [mos-corpora.md](mos-corpora.md):
`src`, `src_sha256`, geometry, `mos`, `mos_std_dev`, `corpus`,
`corpus_version`, and ingest timestamp.

CHUG-specific fields are also preserved:

| Field | Meaning |
|---|---|
| `mos_raw_0_100` | Source `mos_j` value from CHUG's 0-100 MOS axis. |
| `chug_video_id` | Hashed video ID used in the S3 URL. |
| `chug_ref` | Reference flag from the CHUG CSV. |
| `chug_bitladder` | Bitrate-ladder label such as `360p_0.2M_`. |
| `chug_resolution` | Manifest resolution label. |
| `chug_bitrate_label` | Manifest bitrate label. |
| `chug_orientation` | Portrait / landscape label. |
| `chug_framerate_manifest` | Manifest framerate before ffprobe correction. |
| `chug_content_name` | Source content name from the CHUG CSV. |
| `chug_height_manifest`, `chug_width_manifest` | Manifest geometry. |

CHUG's source MOS is 0-100. The adapter maps trainer-facing `mos` onto
`[1, 5]` with:

```text
mos = 1 + 4 * mos_raw_0_100 / 100
```

The raw value remains available for future aggregation paths that use a
0-100 MOS axis directly.

## Local Baseline Training

Once `chug.jsonl` exists, materialise feature rows before training:

```bash
PYTHONPATH=ai/src python ai/scripts/chug_extract_features.py \
  --input .workingdir2/chug/chug.jsonl \
  --output .workingdir2/chug/chug_features.jsonl \
  --clips-dir .workingdir2/chug/clips \
  --cache-dir .workingdir2/chug/feature-cache \
  --split-manifest .workingdir2/chug/chug_splits.json \
  --audit-output .workingdir2/chug/chug_hdr_audit.json \
  --vmaf-bin libvmaf/build-cpu/tools/vmaf \
  --feature-set canonical \
  --full
```

The materialiser pairs each distorted ladder row with the matching
`chug_content_name` reference row, decodes both clips as 10-bit 4:2:0
YUV, scales the distorted side to the reference geometry, runs libvmaf,
and writes clip-level feature aggregates. The trainer-facing feature row
contains the canonical bare feature names (`adm2`, `vif_scale0` ...
`motion2`) as means, plus `<feature>_mean`, `<feature>_p10`,
`<feature>_p90`, and `<feature>_std` columns for downstream sweeps.

The materialiser assigns train/validation/test splits at
`chug_content_name` granularity, not at row granularity. Every bitrate
ladder variant for a source content therefore stays in one split, which
prevents reference-content leakage across validation. The default policy
is deterministic `80/10/10` BLAKE2s hashing with seed `chug-hdr-v1`; use
`--split train`, `--split val`, or `--split test` to materialise one
partition, and `--split-manifest` to write the local content-to-split
map.

`--audit-output` writes a local ffprobe HDR metadata audit before
feature extraction. The audit records row counts, probe failures,
transfer-characteristic counts (`pq`, `hlg`, `sdr`, `unknown`),
primaries, pix-fmt distribution, split row counts, and malformed HDR
rows where PQ/HLG is signalled without BT.2020 primaries. This is the
first check to run before using a CHUG feature file for HDR experiments.

Train against the feature rows:

```bash
python ai/scripts/train_konvid_mos_head.py \
  --konvid-1k .workingdir2/chug/chug_features.jsonl \
  --konvid-150k .workingdir2/chug/no-konvid-150k.jsonl \
  --out-onnx .workingdir2/chug/chug_mos_head.onnx \
  --out-card .workingdir2/chug/chug_mos_head_card.md \
  --out-manifest .workingdir2/chug/chug_mos_head.json
```

When feature rows carry the `split` column emitted by
`chug_extract_features.py`, `train_konvid_mos_head.py` uses that
content-level split for validation instead of creating random k-folds.
The exported local checkpoint is trained on the `train` partition only,
leaving `val` / `test` rows held out for calibration and reporting.

This is a baseline unlock, not a final HDR model. The CHUG feature rows
carry full-reference libvmaf features, but future production HDR claims
still need the HDR teacher/model decision to land.

## Local FULL_FEATURES Experiments

For local CHUG sweeps that use the generic FR-from-NR FULL_FEATURES
extractor, point `ai/scripts/extract_k150k_features.py` at CHUG clips and
labels:

```bash
PYTHONPATH=ai/src python ai/scripts/extract_k150k_features.py \
  --clips-dir .workingdir2/chug/clips \
  --scores .workingdir2/chug/chug_scores.csv \
  --metadata-jsonl .workingdir2/chug/chug.jsonl \
  --vmaf-bin libvmaf/build-cuda/tools/vmaf \
  --cpu-vmaf-bin libvmaf/build-cpu/tools/vmaf \
  --out .workingdir2/chug/training/full_features_chug.parquet \
  --scratch-dir .workingdir2/chug/feature_scratch_cuda
```

When a CUDA binary is used, the extractor splits the pass: explicit CUDA
feature names for the stable CUDA twins, then a CPU residual pass for
`float_ssim` and `cambi`. This avoids the mixed all-feature
`--backend cuda` CLI path that can fail with duplicate feature-key writes
while preserving the same parquet columns for training.
With `--metadata-jsonl`, the parquet also carries CHUG content identity,
bitrate-ladder fields, raw 0-100 MOS, and the same deterministic
content-level split metadata used by the JSONL materialiser.

Train directly from that FULL_FEATURES parquet once extraction finishes:

```bash
python ai/scripts/train_konvid_mos_head.py \
  --feature-parquet .workingdir2/chug/training/full_features_chug.parquet \
  --out-onnx .workingdir2/chug/chug_full_features_mos_head.onnx \
  --out-card .workingdir2/chug/chug_full_features_mos_head_card.md \
  --out-manifest .workingdir2/chug/chug_full_features_mos_head.json
```

`train_konvid_mos_head.py` reads both bare trainer columns (`adm2`) and
FULL_FEATURES aggregate columns (`adm2_mean`). If the parquet was written
with `--metadata-jsonl`, the trainer also honors the `split` column so the
CHUG content-level holdout survives the parquet path.

If a long-running extraction was started without `--metadata-jsonl`, do
not rerun the feature pass just to recover split metadata. Enrich the
finished parquet in place from `chug.jsonl`:

```bash
python ai/scripts/enrich_k150k_parquet_metadata.py \
  --features-parquet .workingdir2/chug/training/full_features_chug.parquet \
  --metadata-jsonl .workingdir2/chug/chug.jsonl
```

The enrichment utility matches rows by `clip_name`, fills missing CHUG
side columns, and leaves existing feature/MOS columns untouched unless
`--overwrite-metadata` is passed.
