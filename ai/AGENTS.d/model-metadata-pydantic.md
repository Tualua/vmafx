---
paths:
  - ai/src/vmaf_train/registry.py
  - ai/src/vmaf_train/train.py
  - ai/src/vmaf_train/data/datasets.py
invariant: Operator-facing TrainConfig, ModelMetadata, ManifestEntry use pydantic v2 BaseModels; internal stay @dataclass.
---
<!-- markdownlint-disable MD013 MD060 -->
# Pydantic models for operator inputs

- **`vmaf_train.train.TrainConfig`, `vmaf_train.registry.ModelMetadata`,
  and `vmaf_train.data.datasets.ManifestEntry` are pydantic v2
  `BaseModel`s, not `@dataclass`es.** (ADR-0934.) They parse
  operator-supplied YAML / JSON, so boundary needs declared
  validators + `extra="forbid"` + line-numbered errors. Every other
  dataclass in `ai/src/vmaf_train/` (`NormReport`, `BisectResult`,
  `EvalReport`, `CrossBackendReport`, `ModelAudit`, `ProfileReport`,
  `QuantizationReport`, `AllowlistReport`, `Splits`, `Entry`, etc.)
  is producer-controlled and stays as `@dataclass` on purpose — do
  not mass-convert "for consistency". Rebase rule: new
  dataclass ingesting `yaml.safe_load(...)` or `json.loads(...)`
  output via `**doc` or `cls(field=doc["field"], ...)` becomes
  `BaseModel`; new dataclass produced by typed function call
  stays `@dataclass`. `ModelMetadata.to_json()` round-trips via
  `model_dump(mode="json")` + `json.dumps(indent=2, sort_keys=True)`;
  do not switch it to `BaseModel.model_dump_json()` (different
  formatting — would invalidate sidecar goldens).
