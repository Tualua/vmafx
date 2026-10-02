---
paths:
  - ai/scripts/konvid_150k_to_corpus_jsonl.py
  - ai/scripts/lsvq_to_corpus_jsonl.py
  - ai/scripts/youtube_ugc_to_corpus_jsonl.py
  - ai/scripts/waterloo_ivc_to_corpus_jsonl.py
invariant: MOS corpus adapters share row schema; manifest-csv strict; cross-corpus rescaling is trainer-side concern.
---
<!-- markdownlint-disable MD013 MD060 -->
# MOS corpus ingestion adapters

## KonViD-150k MOS-corpus ingestion (ADR-0325)

**Script:** `ai/scripts/konvid_150k_to_corpus_jsonl.py`

### Rebase-sensitive invariants

- Adapter accepts two local layouts under `.corpus/konvid-150k/`:
  URL `manifest.csv` plus `clips/`, or split score-drop layout
  `k150ka_scores.csv` / `k150kb_scores.csv` plus
  `k150ka_extracted/` / `k150kb_extracted/`. Do not remove split
  discovery path unless staged corpus is migrated first.
- Explicit `--manifest-csv` remains strict. That file missing ->
  adapter must fail instead of falling back to split discovery; this
  catches typoed operator paths.
- Emitted JSONL schema is still shared MOS-corpus schema. Split
  score rows do not add `split` column to output; missing score-drop
  metadata is represented as `mos_std_dev = 0.0` and `n_ratings = 0`.

- MOS-corpus row schema emitted by
  `ai/scripts/lsvq_to_corpus_jsonl.py` (ADR-0367) is byte-identical
  to KonViD-150k Phase 2 adapter
  (`ai/scripts/konvid_150k_to_corpus_jsonl.py`) modulo   `corpus` and `corpus_version` literals. Both are consumed
  through one trainer-side data loader. Do NOT widen schema
  in only one adapter — adding or removing column means   lockstep edit across both, plus `corpus_version` bump.
- **CHUG display-profile training is trainer-side context, not
  corpus-schema mutation.** `train_chug_hdr_mos_head.py` keeps
  `chug-hdr-wide-v1` as no-profile default, and auto-selects
  `chug-hdr-display-v1` only when `--display-profile-json` is supplied
  without explicit `--feature-schema`. Row-local display columns win
  over target profile so future multi-display HDR corpora remain
  usable. Do not widen CHUG JSONL adapters to carry one operator's
  local panel profile; profiles are recorded in emitted manifest
  with their source sha256.

  `ai/scripts/youtube_ugc_to_corpus_jsonl.py` (ADR-0368) is
  byte-identical to LSVQ adapter
  (`ai/scripts/lsvq_to_corpus_jsonl.py`, ADR-0333) and   KonViD-150k Phase 2 adapter modulo `corpus` and
  `corpus_version` literals. All three are consumed through one
  trainer-side data loader. Do NOT widen schema in only one
  adapter — adding or removing column means lockstep edit
  across all three, plus `corpus_version` bump. synthesised
  bucket-URL path (`--bucket-prefix` flag) is YouTube-UGC-specific
  because canonical `original_videos.csv` ships without   `url` column; do not back-port that synthesis seam to LSVQ
  / KonViD-150k adapters where it would mask manifest-CSV bugs.

  `ai/scripts/waterloo_ivc_to_corpus_jsonl.py` (ADR-0369) is
  byte-identical to LSVQ adapter
  (`ai/scripts/lsvq_to_corpus_jsonl.py`, ADR-0333) and   KonViD-150k Phase 2 adapter
  (`ai/scripts/konvid_150k_to_corpus_jsonl.py`, ADR-0325 Phase 2)
  modulo `corpus` and `corpus_version` literals. All three
  adapters are consumed through one trainer-side data loader. Do
  NOT widen schema in only one adapter — adding or removing
  column means lockstep edit across all three (plus   `corpus_version` bump on each). Waterloo IVC adapter
  records MOS verbatim on dataset's native **0–100** scale,
  diverging from KonViD / LSVQ's 1–5 Likert scale; cross-corpus
  rescaling is trainer-side concern and is NOT applied at
  ingest time on either adapter. Trainer-side normaliser
  must read each row's `corpus` literal to pick correct
  per-shard rescale factor.
