<!-- markdownlint-disable MD013 MD060 -->
# ADR-1546: The registry validator holds tiny-model metadata to the shipped graphs

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: ai, tiny-ai, ci, supply-chain, fork-local

## Context

The docs audit found metadata that disagreed with the graphs it describes
(defects 25, 27, 28): `nr_metric_v1` recorded opset 17 in the registry and
sidecar while its fp32 and int8 files import opset 18 (torch's dynamo exporter
raises a requested 17 to 18, and `export_tiny_models.py` wrote 17 regardless);
`fr_regressor_v2`'s registry and sidecar notes described an 8-D codec block for
a 14-wide input, and the trainer's defaults (`--hidden 16 --depth 2`) build a
smaller model than the shipped 3 x 32 one that ADR-0291 records; the five
`fr_regressor_v2_ensemble_v1_seed*` sidecars carried another graph's sha256 and
a 14-slot `encoder_vocab` for 6-wide one-hot inputs; `transnet_v2.json` named an
output the graph does not have; the registry schema said the runtime checks
digests and that `id` is a `--tiny-model` value (it checks neither) and
rejected the `release_url` field that `scripts/ai/fetch-tiny-blobs.sh` reads.

`ai/scripts/validate_model_registry.py`, a required CI check, compared sha256
values only. Its job installs `jsonschema` and nothing else, so it cannot load
graphs with the `onnx` package.

`ai/scripts/build_calibration_set.py` (defect 29) was a stub that exited 1.
`ptq_static.py`'s docstring and `docs/ai/quantization.md` named it as the
future producer of the calibration `.npz`; the stub `quantize_int8.py` named
it as well.

## Decision

We will read every registered graph (and its int8 sibling) in the validator
with a dependency-free protobuf reader, `ai/src/aiutils/onnx_signature.py`,
and fail when the registry `opset` or a sidecar's `opset`, `sha256`, tensor
names, feature-list length or codec-block width disagrees with the graph. The
metadata is corrected to the graphs, the exporters record the opset the file
imports, the `fr_regressor_v2` trainer defaults become the shipped shape, and
the schema describes the runtime as it is and accepts `release_url`. We will
remove `build_calibration_set.py`: static PTQ calibrated from a parquet feature
cache already exists as `vmaf-train quantize-int8` (`vmaf_train/quantize.py`),
no shipped model uses static PTQ (all four quantised models are dynamic), and
a second calibration builder would duplicate it.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Dependency-free reader in the validator (chosen) | Runs in the existing required job; matches `onnx` on all 30 shipped files | A small protobuf walker to maintain | — |
| Install `onnx` in the registry job | Official parser | A native wheel in a 5-minute lint job; version pins to maintain | Heavier than a reader of about 200 lines |
| Fix the metadata once, add no check | Smallest change | The same drift returns with the next export | The audit found five kinds of drift |
| Implement `build_calibration_set.py` | Keeps the documented tool | Duplicates `vmaf-train quantize-int8`'s calibration path; nothing consumes static PTQ today | One behaviour, one implementation (HISS-19) |
| Rewrite the `fr_regressor_v2` sidecar only, keep the 16 x 2 defaults | No trainer change | A default run still trains a model other than the shipped one | The defaults should reproduce what ships |

## Consequences

- **Positive**: a sidecar or registry row that disagrees with its graph fails
  CI; the validator found 20 such errors on master.
- **Negative**: a re-export must keep the sidecar in step with the graph or
  the job fails.
- **Neutral / follow-ups**: the ensemble seeds stay smoke models until the
  one-shot retrain (ADR-1105); `gen_calibration.py` and `quantize_int8.py`
  remain "not yet implemented" stubs.

## References

- `Q` (maintainer popup, 2026-10-04, paraphrased): fix every docs-audit defect
  now; registry and sidecar metadata match the shipped ONNX (opset, shapes,
  codec block width), the registry schema accepts what the registry holds and
  describes the CLI truthfully, and `build_calibration_set.py` is implemented
  or removed with its references.
- Docs-audit defects 25, 27, 28, 29.
- [ADR-0211](0211-model-registry-sigstore.md),
  [ADR-0291](0291-fr-regressor-v2-prod-ship.md),
  [ADR-0321](0321-fr-regressor-v2-ensemble-full-prod-flip.md),
  [ADR-1105](1105-ensemble-v2-prod-flip-deferred-oneshot-retrain.md),
  [ADR-1520](1520-tiny-model-feature-inputs-at-flush.md).
