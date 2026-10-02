---
paths:
  - ai/scripts/build_bisect_cache.py
  - ai/testdata/bisect/*
  - ai/src/vmaf_train/bisect_model_quality.py
invariant: Bisect cache fixture is content-stable; logical pyarrow Table equality for parquet, byte-exact for ONNX.
---
<!-- markdownlint-disable MD013 MD060 -->
# Nightly bisect-model-quality cache

- **Bisect-cache fixture is content-stable** — `ai/testdata/bisect/`
  = deterministic default for nightly `bisect-model-quality`
  workflow. Regenerate committed synthetic cache via
  `python ai/scripts/build_bisect_cache.py` with seeds
  `FEATURE_SEED=20260418` / `MODEL_SEED=20260419`. Same script can
  materialise real DMOS/MOS-aligned parquet via `--source-features` +
  optional `--target-column`; that path must preserve canonical-six
  feature order and still normalise output target column to `mos`.
  CI runs script with `--check`.

  As of ADR-0262, parquet leg of check uses logical
  `pyarrow.Table.equals` content comparison (schema + row count +
  values), tolerating writer-version-string drift in `created_by`
  parquet header. ONNX still compares byte-for-byte via
  `filecmp.cmp(shallow=False)`, so ONNX-side determinism must stay
  intact. **Do not** remove
  `model.producer_name = "vmaf-train.bisect-cache"`,
  `model.producer_version = "1"`, or
  `model.ir_version = 9` pins in `_save_linear_fr`: those three
  lines stabilise ONNX bytes across `onnx` minor versions. See
  [ADR-0262](../../docs/adr/0262-bisect-cache-logical-comparison.md) +
  [ADR-0109](../../docs/adr/0109-nightly-bisect-model-quality.md) +
  [Research-0001](../../docs/research/0001-bisect-model-quality-cache.md).

- [ADR-0109](../../docs/adr/0109-nightly-bisect-model-quality.md) — nightly bisect workflow + synthetic placeholder cache.
