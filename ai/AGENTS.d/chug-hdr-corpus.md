---
paths:
  - ai/scripts/chug_to_corpus_jsonl.py
  - ai/scripts/chug_extract_features.py
  - ai/scripts/enrich_k150k_parquet_metadata.py
invariant: CHUG HDR data local-only; mos_raw_0_100 mapped to [1, 5]; content-level 80/10/10 deterministic splits.
---
<!-- markdownlint-disable MD013 MD060 -->
# CHUG HDR MOS-corpus ingestion (ADR-0426)

**Script:** `ai/scripts/chug_to_corpus_jsonl.py`

## Rebase-sensitive invariants

- CHUG data is local-only under `.corpus/chug/`. Do not commit   public `chug.csv`, downloaded MP4s, emitted JSONL, trained local
  CHUG heads, or derived features. README/license mismatch is
  handled by treating dataset as non-commercial/share-alike until
  clarified.
- CHUG's public `mos_j` column on 0-100 axis. Adapter preserves
  it as `mos_raw_0_100` and maps trainer-facing `mos` onto `[1, 5]`
  via `1 + 4 * mos_raw_0_100 / 100` so existing MOS-head trainer
  does not drop every row as out-of-range. Do not remove raw field
  or silently change scale.
- Adapter preserves CHUG HDR / ladder metadata (`chug_bitladder`,
  `chug_resolution`, `chug_bitrate_label`, orientation, manifest
  geometry, and source content name) as optional JSONL fields. Existing
  MOS-head training ignores those columns today; future HDR models may
  consume them explicitly.
- CHUG feature materialiser is governed by ADR-0427. It pairs each
  distorted row with matching `chug_content_name` reference row,
  decodes both sides as 10-bit 4:2:0, and scales distorted side to
  reference geometry before libvmaf extraction. Changing that alignment
  policy changes training distribution and requires new ADR.
- CHUG train/validation/test splits are content-level, not row-level.
  `ai/scripts/chug_extract_features.py` hashes `chug_content_name` with
  seed `chug-hdr-v1` into deterministic 80/10/10 partitions and writes
  chosen `split` plus `chug_split_key` into every feature row. Do
  not split bitrate-ladder rows independently; that leaks same
  source content across validation.
- Local HDR metadata audit (`--audit-output`) is pre-training
  gate for CHUG experiments. Preserve its ffprobe transfer / primaries /
  pix-fmt counters and malformed-PQ/HLG-without-BT.2020 row list when
  touching materialiser.
- Per ADR-0651, CHUG feature materialiser also writes per-row
  `feature_ref_*` and `feature_dis_*` HDR/display metadata copied from
  ffprobe (`codec_name`, `pix_fmt`, `color_transfer`, normalized
  `transfer_class`, primaries, colorspace/range, MaxCLL/MaxFALL-style
  static metadata). Preserve unknown/null values explicitly; do not
  infer display-panel capability from clip metadata alone.
- Per ADR-0652, same decode pass writes luma-domain visual-signal
  primitives (`luma_std`, `sharpness_laplacian_var`,
  `highfreq_abs_mean`, `noise_lap_mad`) for both reference and
  distorted clips plus `feature_delta_*` distorted-minus-reference
  fields. These are diagnostic blur/noise/grain proxies; do not treat
  them as replacement for trained NR VQA model.
- `ai/scripts/enrich_k150k_parquet_metadata.py` is recovery path for
  FULL_FEATURES parquet jobs that were started without `--metadata-jsonl`.
  It must match metadata by `clip_name` / JSONL basename, fill missing
  metadata cells by default, and keep feature/MOS columns unchanged unless
  `--overwrite-metadata` is explicitly passed.
