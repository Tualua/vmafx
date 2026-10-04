<!-- markdownlint-disable MD060 -->
# YouTube UGC -> MOS-corpus JSONL ingestion

This page documents how to build the YouTube UGC MOS-corpus shard
consumed by the fork's no-reference and MOS-head trainers. It covers the
YouTube UGC shard ingestion adapter
(`ai/scripts/youtube_ugc_to_corpus_jsonl.py`, ADR-0413).

## What YouTube UGC is

The Google YouTube UGC dataset (Wang, Inguva, Adsumilli; MMSP
2019) is the field's canonical large-scale UGC corpus. ~1500
original community-uploaded clips spanning gaming, vlogs,
lyric-videos, sports, HDR, and animation, with crowd MOS values
on the same 1.0-5.0 Likert scale as LSVQ / KonViD.

- **Public bucket**:
  <https://storage.googleapis.com/ugc-dataset/>
  (CC-BY, no sign-up, no request form).
- **Original-video listing CSV**:
  <https://storage.googleapis.com/ugc-dataset/original_videos.csv>.
- **Attribution / license**:
  <https://storage.googleapis.com/ugc-dataset/ATTRIBUTION>.

## When to run the adapter

Run this adapter when you want to (re-)build the YouTube UGC
MOS-corpus JSONL shard the trainer consumes alongside LSVQ
(ADR-0367), KonViD-150k (ADR-0325 Phase 2), and BVI-DVC
(ADR-0310).

## Prerequisites

1. ~2 TB free disk under `.corpus/youtube-ugc/` for the
   whole-corpus run, or ~10 GB for the laptop-class default.
2. `curl` and `ffprobe` on `$PATH`.
3. The manifest CSV from the bucket dropped at
   `.corpus/youtube-ugc/manifest.csv` (or pass
   `--manifest-csv` to point elsewhere).

## Quick start (laptop-class subset)

```bash
# Drop original_videos.csv at the default location, then:
python ai/scripts/youtube_ugc_to_corpus_jsonl.py
# -> reads .corpus/youtube-ugc/manifest.csv
# -> caps at the first --max-rows=300 clips (default)
# -> downloads each via curl into .corpus/youtube-ugc/clips/
# -> writes .corpus/youtube-ugc/youtube-ugc.jsonl
```

If the manifest CSV does not carry a URL column (the canonical
`original_videos.csv` does not), the adapter synthesises the
download URL from `--bucket-prefix` (default
`https://storage.googleapis.com/ugc-dataset/original_videos/`).

## Whole-corpus ingestion

```bash
python ai/scripts/youtube_ugc_to_corpus_jsonl.py --full
```

This disables the `--max-rows` cap. Working set is ~2 TB on the
canonical original-videos manifest. The run is resumable:
`Ctrl-C` mid-download is safe, and re-running picks up from
`.corpus/youtube-ugc/.download-progress.json` (atomic
tempfile-rename writes).

## Compressed VP9 subset helper

A separate helper, `ai/scripts/fetch_youtube_ugc_subset.py`, downloads the
VP9 compressed subset. It is not part of the adapter run above.

| Flag | Default | Meaning |
|---|---|---|
| `--out-dir` | required | Directory the downloaded mp4/webm files go into |
| `--n-stems` | `30` | Pick the N smallest complete 4-tuple stems |
| `--manifest` | required | Output JSON manifest (stem to `{orig,cbr,vod,vodlb}`); keeps its existing shape for downstream consumers |
| `--run-manifest-out` | `<manifest>.run-manifest.json` | Run sidecar; point it into a dated experiment bundle if needed |

The run sidecar records the GCS listing URL, the smallest-complete-4tuple
selection policy, selected file sizes, output directory, and ADR-0661
`run_provenance`.

## Output schema

One JSON object per line in
`.corpus/youtube-ugc/youtube-ugc.jsonl`:

```jsonc
{
  "src":               "Gaming_720P-25aa_orig.mp4",
  "src_sha256":        "<hex>",
  "src_size_bytes":    1234567,
  "width":             1280,
  "height":            720,
  "framerate":         30.0,
  "duration_s":        20.0,
  "pix_fmt":           "yuv420p",
  "encoder_upstream":  "h264",
  "mos":               3.42,
  "mos_std_dev":       0.51,
  "n_ratings":         48,
  "corpus":            "youtube-ugc",
  "corpus_version":    "ugc-2019-orig",
  "ingested_at_utc":   "2026-05-08T10:00:00+00:00"
}
```

The schema is byte-identical to the LSVQ / KonViD-150k
adapters' modulo the `corpus` and `corpus_version` literals. MOS
is recorded verbatim on the dataset's native 1.0-5.0 scale (no
rescaling at ingest time); the trainer-side data loader is
responsible for any per-corpus normalisation.

## Per-clip scoring methodology

Two distinct subjective releases sit under the same dataset
umbrella:

1. **2019 originals release** (default,
   `--corpus-version=ugc-2019-orig`) — per-clip crowd MOS on the
   1.0-5.0 Likert scale across 1380 of the ~1500 originals.
   Pass-through identical to LSVQ.
2. **2020 transcoded follow-up**
   (`--corpus-version=ugc-2020-transcoded-mean`) — per-bitrate
   crowd ratings on transcoded outputs at four rate points
   (`orig` / `cbr` / `vod` / `vodlb`). Operators wanting these
   ratings pre-aggregate them into a one-row-per-`orig` CSV with
   the per-clip mean across the four levels; the adapter records
   the mean verbatim.

The adapter records whatever the manifest's MOS column contains,
without rescaling. Documenting the methodology behind the
manifest's MOS column is the operator's responsibility (it
propagates through the row's `corpus_version` literal).

## Manifest CSV column-name aliases

YouTube UGC manifest CSVs ship with slightly different header
spellings across the 2019 MMSP release, the 2020 transcoded
follow-up, and DOVER / FAST-VQA redistributions. The adapter
accepts every observed alias:

| Logical column | Recognised header spellings |
|---|---|
| filename | `vid`, `name`, `video_name`, `filename`, `file_name` |
| URL (optional) | `url`, `download_url`, `video_url` |
| MOS | `mos`, `MOS`, `mos_score`, `dmos`, `DMOS` |
| MOS std-dev | `sd`, `SD`, `mos_std`, `mos_std_dev`, `sd_mos`, `SD_MOS` |
| rating count | `n`, `ratings`, `num_ratings`, `n_ratings` |

Bare-stem filenames (e.g. `Gaming_720P-25aa_orig`) are normalised
to `Gaming_720P-25aa_orig.mp4` by appending `--clip-suffix`
(default `.mp4`).

## Operator flags

| Flag | Default | Meaning |
|---|---|---|
| `--ugc-dir` | `.corpus/youtube-ugc/` | Working directory |
| `--manifest-csv` | `<dir>/manifest.csv` | Path to the manifest CSV |
| `--progress-path` | `<dir>/.download-progress.json` | Resumable state file |
| `--clips-subdir` | `clips` | Subdirectory for clips |
| `--clip-suffix` | `.mp4` | Default file suffix |
| `--bucket-prefix` | `original_videos/` on the ugc-dataset bucket | Public bucket URL prefix used to synthesise download URLs when the manifest lacks a `url` column |
| `--output` | `<dir>/youtube-ugc.jsonl` | Output JSONL |
| `--manifest-out` | `<output>.manifest.json` | Replay manifest JSON sidecar |
| `--ffprobe-bin` | `$FFPROBE_BIN` or `ffprobe` | ffprobe binary |
| `--curl-bin` | `$CURL_BIN` or `curl` | curl binary |
| `--corpus-version` | `ugc-2019-orig` | Dataset version; pass `ugc-2020-transcoded-mean` for the transcoded-mean variant |
| `--attrition-warn-threshold` | `0.10` | Advisory failure-rate floor |
| `--download-timeout-s` | `300` | Per-clip `curl --max-time` seconds (UGC clips are large) |
| `--max-rows` | `300` | Row cap (laptop-class subset) |
| `--full` | off | Disable the `--max-rows` cap; ingest the whole CSV |
| `--log-level` | `INFO` | `DEBUG`, `INFO`, `WARNING` or `ERROR` |

The replay manifest records the UGC working directory, manifest/progress paths,
row cap, attrition counters, effective corpus version, and ADR-0661
`run_provenance`.

## Failure handling

- **Download failure** (HTTP 404 / 410, curl spawn failure,
  empty body): logged with the reason, persisted to the progress
  file as `state: "failed"`, run continues. Re-runs honour the
  non-retry contract — to retry, delete the entry from the
  progress file or delete the whole file.
- **ffprobe failure** ("broken-clip"): logged, run continues,
  no row emitted. Distinct from download-failed in the summary
  line.
- **Attrition WARNING**: when the download-failed fraction
  exceeds `--attrition-warn-threshold` (default 10%), an
  advisory WARNING is logged. The run still completes.

## License & redistribution

YouTube UGC is Creative Commons Attribution. This fork ships
the adapter and the schema in tree, but **never** the raw clips,
the per-clip MOS values, or any derived feature cache. Only trained
model weights derived from the corpus can ship, with CC-BY
attribution travelling alongside.

## Related

- [ADR-0413: YouTube UGC corpus
  ingestion](../adr/0413-youtube-ugc-corpus-ingestion.md).
- [Research-0091: YouTube UGC corpus
  feasibility](../research/0091-youtube-ugc-corpus-feasibility.md).
- [ADR-0367](../adr/0367-lsvq-corpus-ingestion.md) (LSVQ) —
  same adapter shape; this YouTube UGC adapter is a near-mirror
  modulo dataset specifics + the synthesised-bucket-URL path.
- ADR-0325 Phase 2 (KonViD-150k) — schema co-author.
- [ADR-0310](../adr/0310-bvi-dvc-corpus-ingestion.md) (BVI-DVC) —
  the first second-shard ingestion ADR; sets the
  local-only-corpus / redistributable-derivatives precedent.
