---
paths:
  - ai/scripts/materialize_mos_labels.py
  - ai/scripts/batch_materialize_mos_labels.py
invariant: MOS labels are explicit real-training inputs; batch manifests orchestrate joins without duplicating logic.
---
<!-- markdownlint-disable MD013 MD060 -->
# MOS label materialization

- [ADR-0663](../../docs/adr/0663-mos-label-materializer.md) — **MOS labels are explicit real-training inputs.** `ai/scripts/materialize_mos_labels.py` joins subjective MOS labels onto already-extracted feature tables, stays table-side: no feature extraction, corpus downloading, or training. `train_konvid_mos_head.py` must not silently synthesize data when explicit real-corpus paths produce zero labelled rows; use `--smoke` for synthetic CI/load-path checks and reject low-coverage real joins unless operator deliberately lowers materializer threshold.
- [ADR-0675](../../docs/adr/0675-mos-label-materializer-batch-manifest.md) — **MOS-label batch manifests only orchestrate joins.** `ai/scripts/batch_materialize_mos_labels.py` may select multiple feature tables and label sources. Must call shared table-side materializer; must not duplicate MOS parsing, key-normalisation, match-rate, or overwrite logic.
