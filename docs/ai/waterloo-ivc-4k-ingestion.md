<!-- markdownlint-disable MD060 -->
# Waterloo IVC 4K-VQA → MOS-corpus JSONL ingestion

This page documents how to build the Waterloo IVC 4K-VQA MOS-corpus shard
consumed by the fork's no-reference and MOS-head trainers. It covers the
shard ingestion adapter
(`ai/scripts/waterloo_ivc_to_corpus_jsonl.py`, ADR-0369).

## What Waterloo IVC 4K-VQA is

Waterloo IVC 4K-VQA — the University of Waterloo Image and
Vision Computing Laboratory's 4K Video Quality Database
(Li, Duanmu, Liu, Wang; ICIAR 2019) — is a
controlled-subjective-study video-quality corpus with
2160p coverage. Twenty pristine 4K source sequences are
re-encoded with five contemporary codecs at three
resolutions and four distortion levels, yielding 1 200
distorted clips with per-clip MOS.

- **Sources**: 20 pristine 2160p sequences.
- **Encoders**: H.264/AVC, H.265/HEVC, VP9, AVS2, AV1.
- **Resolutions**: 540p / 1080p / 2160p.
- **Distortion levels**: 4 per (encoder, resolution).
- **Total distorted clips**: 1 200.
- **MOS scale**: 0–100 native (not 1–5 Likert; see
  "Cross-corpus MOS scale caveat" below).
- **Dataset card**:
  <https://ivc.uwaterloo.ca/database/4KVQA.html>.
- **Archive base**:
  <https://ivc.uwaterloo.ca/database/4KVQA/201908/>.
- **Licence**: Permissive academic — attribution required,
  no NDA, no password gate, no registration form.

## When to run the adapter

Run this adapter when you want to (re-)build the
Waterloo IVC 4K-VQA MOS-corpus JSONL shard the trainer
consumes alongside BVI-DVC, KonViD-150k, and LSVQ.

## Prerequisites

1. Multi-TB free disk under `.corpus/waterloo-ivc-4k/`
   for the whole-corpus run, or ~few-hundred GB for the
   laptop-class default.
2. `curl` and `ffprobe` on `$PATH`.
3. The scores table from
   <https://ivc.uwaterloo.ca/database/4KVQA/201908/scores.txt>
   dropped at `.corpus/waterloo-ivc-4k/manifest.csv`
   (or pass `--manifest-csv` to point elsewhere).
4. The bulk archives (Sources, H264, HEVC, VP9, AVS2 / AV1
   split-4-part) extracted to
   `.corpus/waterloo-ivc-4k/clips/`. The canonical
   `scores.txt` carries no per-clip URL column, so the
   adapter expects clips to be staged on disk before it
   runs (the upstream's bulk-archive distribution model).

## Quick start (laptop-class subset)

```bash
# Drop the upstream scores.txt and the extracted bulk
# archives at the default location, then:
python ai/scripts/waterloo_ivc_to_corpus_jsonl.py
# → reads .corpus/waterloo-ivc-4k/manifest.csv
# → caps at the first --max-rows=100 clips (default)
# → probes each clip via ffprobe
# → writes .corpus/waterloo-ivc-4k/waterloo_ivc_4k.jsonl
```

## Whole-corpus ingestion

```bash
python ai/scripts/waterloo_ivc_to_corpus_jsonl.py --full
```

This disables the `--max-rows` cap. Working set is
multi-TB end-to-end on the canonical 1 200-clip distorted
set + 20 pristine 4K sources.

## Output schema

One JSON object per line in
`.corpus/waterloo-ivc-4k/waterloo_ivc_4k.jsonl`:

```jsonc
{
  "src":               "HEVC_1_540p_1.yuv",
  "src_sha256":        "<hex>",
  "src_size_bytes":    1234567,
  "width":             3840,
  "height":            2160,
  "framerate":         30.0,
  "duration_s":        10.0,
  "pix_fmt":           "yuv420p10le",
  "encoder_upstream":  "hevc",
  "mos":               18.21,
  "mos_std_dev":       0.0,
  "n_ratings":         0,
  "corpus":            "waterloo-ivc-4k",
  "corpus_version":    "waterloo-ivc-4k-201908",
  "ingested_at_utc":   "2026-05-08T10:00:00+00:00"
}
```

The schema is byte-identical to the KonViD-150k Phase 2
and LSVQ adapters' modulo the `corpus` and
`corpus_version` literals. MOS is recorded verbatim on
the Waterloo-native 0–100 scale (no rescaling at ingest
time); see the cross-corpus caveat below.

The canonical headerless `scores.txt` carries neither a
MOS standard deviation nor a rating count — those columns
are absent upstream. The adapter records `mos_std_dev =
0.0` and `n_ratings = 0` for canonical-shape rows; if an
operator pre-mangles the scores file into the standard
LSVQ-shape CSV with explicit `sd` / `n` columns, those
round-trip verbatim.

## Cross-corpus MOS scale caveat

Waterloo IVC 4K-VQA records MOS on **0–100 raw**, while KonViD-150k and
LSVQ are on **1–5 Likert**. The adapter records the score verbatim on its
native scale (no ingest-time rescaling), matching the policy of LSVQ and
KonViD-150k.

| `corpus` value | MOS scale in the shard |
|---|---|
| `waterloo-ivc-4k` | 0–100 |
| `konvid-150k`, `lsvq` | 1–5 |

!!! warning
    Do not concatenate shards of different scales and train on them
    directly. Per-corpus normalisation is done by
    `ai/scripts/aggregate_corpora.py`, which maps every corpus onto a unified
    0–100 axis (Waterloo is the identity mapping); see
    [multi-corpus-aggregation.md](multi-corpus-aggregation.md) for the
    conversions. See also ADR-0369 §Consequences and
    [`docs/rebase-notes.md`](../rebase-notes.md).

## Manifest CSV shapes

The adapter accepts two manifest shapes; auto-detection
sniffs the first row.

### Canonical headerless 5-tuple (upstream `scores.txt`)

The upstream `scores.txt` is **headerless**, with five
comma-separated columns:

```text
encoder, video_number, resolution, distortion_level, mos
```

Sample rows verbatim:

```text
HEVC, 1, 540p, 1, 18.21
HEVC, 1, 540p, 2, 39.46
HEVC, 1, 540p, 3, 50.23
HEVC, 1, 540p, 4, 77.26
HEVC, 1, 1080p, 1,  7.61
```

The adapter synthesises the on-disk filename via the
convention `{encoder}_{video_number}_{resolution}_
{distortion}.<suffix>` (default `.yuv`) and looks the
clip up under `clips/`.

### Standard LSVQ / KonViD-150k header

When an operator pre-mangles `scores.txt` into a
named-column CSV with the LSVQ-shape header
(`name,url,mos,sd,n`), the adapter parses it through the
standard branch. Aliases:

| Logical column | Recognised header spellings |
|---|---|
| filename | `name`, `video_name`, `filename`, `file_name` |
| URL (optional) | `url`, `download_url`, `video_url` |
| MOS | `mos`, `MOS`, `mos_score` |
| MOS std-dev | `sd`, `SD`, `mos_std`, `mos_std_dev`, `SD_MOS` |
| rating count | `n`, `ratings`, `num_ratings`, `n_ratings` |

## Operator flags

| Flag | Default | Meaning |
|---|---|---|
| `--waterloo-ivc-dir` | `.corpus/waterloo-ivc-4k/` | Working directory |
| `--manifest-csv` | `<dir>/manifest.csv` | Path to the manifest |
| `--progress-path` | `<dir>/.download-progress.json` | Resumable state file |
| `--clips-subdir` | `clips` | Subdirectory for clips |
| `--clip-suffix` | `.yuv` | Default file suffix |
| `--output` | `<dir>/waterloo_ivc_4k.jsonl` | Output JSONL |
| `--manifest-out` | `<output>.manifest.json` | Replay manifest JSON sidecar |
| `--ffprobe-bin` | `$FFPROBE_BIN` or `ffprobe` | ffprobe binary |
| `--curl-bin` | `$CURL_BIN` or `curl` | curl binary |
| `--corpus-version` | `waterloo-ivc-4k-201908` | Dataset version |
| `--attrition-warn-threshold` | `0.10` | Advisory failure-rate floor |
| `--download-timeout-s` | `120` | Per-clip `curl --max-time` seconds |
| `--max-rows` | `100` | Row cap (laptop-class subset) |
| `--full` | off | Disable the `--max-rows` cap; ingest the whole CSV |
| `--log-level` | `INFO` | `DEBUG`, `INFO`, `WARNING` or `ERROR` |

The replay manifest records the Waterloo working directory, manifest/progress
paths, row cap, attrition counters, effective corpus version, and ADR-0661
`run_provenance`.

## Failure handling

- **Download failure** (HTTP 404 / 410, curl spawn
  failure, empty body, missing URL on canonical-shape
  rows): logged with the reason, persisted to the
  progress file as `state: "failed"`, run continues.
  Re-runs honour the non-retry contract — to retry,
  delete the entry from the progress file or delete the
  whole file.
- **ffprobe failure** ("broken-clip"): logged, run
  continues, no row emitted. Distinct from
  download-failed in the summary line.
- **Attrition WARNING**: when the download-failed
  fraction exceeds `--attrition-warn-threshold` (default
  10 %), an advisory WARNING is logged. The run still
  completes. For canonical-shape (no-URL) operations the
  threshold is effectively a "missing-clips" warning —
  every "download failure" really means a clip is not
  staged on disk.

## License & redistribution

Waterloo IVC 4K-VQA is published under the Image and
Vision Computing Laboratory permissive academic licence:

> Permission is granted, without written agreement and
> without license or royalty fees, to use, copy, modify,
> and distribute this database and its documentation for
> any purpose, provided that the copyright notice in its
> entirity appear in all copies and the Image and Vision
> Computing Laboratory (IVC) at the University of
> Waterloo is acknowledged in any publication using the
> database.

This fork ships the adapter and the schema in tree, but
**never** the raw clips, the per-clip MOS values, or any
derived feature cache. Only trained model
weights derived from the corpus can ship, with the IVC
attribution travelling alongside.

Citation: Li, Z., Duanmu, Z., Liu, W., Wang, Z., "AVC,
HEVC, VP9, AVS2 or AV1? — A Comparative Study of
State-of-the-art Video Encoders on 4K Videos," ICIAR
2019.

## Related

- [ADR-0369: Waterloo IVC 4K-VQA corpus
  ingestion](../adr/0369-waterloo-ivc-4k-corpus-ingestion.md).
- [Research-0091: Waterloo IVC 4K-VQA corpus
  feasibility](../research/0091-waterloo-ivc-4k-corpus-feasibility.md).
- [ADR-0367](../adr/0367-lsvq-corpus-ingestion.md) (LSVQ) — same adapter shape;
  this Waterloo
  IVC adapter is a near-mirror modulo dataset specifics
  (manifest shape + MOS scale).
- [ADR-0325 Phase 2](../adr/0325-konvid-150k-corpus-ingestion.md)
  (KonViD-150k) — same schema; same resumable-download contract.
- [ADR-0310](../adr/0310-bvi-dvc-corpus-ingestion.md)
  (BVI-DVC) — first second-shard ingestion ADR; sets the
  local-only-corpus / redistributable-derivatives
  precedent.
