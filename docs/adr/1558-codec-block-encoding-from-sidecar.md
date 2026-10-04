<!-- markdownlint-disable MD013 MD060 -->
# ADR-1558: A codec-aware sidecar declares how its codec block's scalar slots are normalised

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: ai, dnn, tiny-ai, correctness, fork-local

## Context

The codec block of a codec-aware tiny model is `[encoder one-hot, preset_norm,
crf_norm]`. libvmaf filled the two scalar slots with the `fr_regressor_v2`
trainer's normalisation for every model: `preset_ordinal / 9` and
`clamp(crf, 0, 63) / 63`. `fr_regressor_v3`'s trainer
(`ai/scripts/train_fr_regressor_v3.py::_build_codec_block`) sets
`preset_norm` to 0.5 on every row and min-max normalises the CRF over its
training corpus. With `--tiny-codec libx264 --tiny-preset medium --tiny-crf
28` libvmaf handed v3 0.556 and 0.444 where the trainer would have given 0.5
and 0.5, and on the Netflix pair the scores moved from 64.67 / 69.47 / 67.75
(trained encoding) to 66.91 / 71.12 / 69.40. The v3 sidecar recorded neither
the encoding nor the CRF range (state row
`T-TINY-FR-V3-CODEC-NORMALISATION-2026-10-04`).

The training corpus is not in the repository. Its sha256 (`58512e6c...`) is
the one the `fr_regressor_v2` ensemble's production sidecars recorded with
`cq_min` 19 and `cq_max` 37, computed by `_compute_codec_block()` in
`train_fr_regressor_v2_ensemble_loso.py` as the min and max of the `cq`
column; v3 computes the same min and max of the same column for that corpus
shape.

## Decision

We will let a sidecar declare the normalisation: `codec_preset_norm`
(`ordinal`, the default, or `constant` with `codec_preset_value`) and
`codec_crf_norm` (`div63`, the default, or `minmax` with `codec_crf_min` and
`codec_crf_max`). `vmaf_dnn_codec_block_fill_encoded()` fills the block with
it, unclamped for min-max and 0.5 for an empty range as the trainer computes
it; an unknown value or missing bound refuses the sidecar. A constant preset
makes `vmaf_dnn_set_codec_context()` log that the caller's preset has no
effect. `fr_regressor_v3.json` declares constant 0.5 and min-max 19..37, and
the v3 trainer writes the keys from the range it computed.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Sidecar-declared encoding (chosen) | One fill routine; new trainers describe themselves; v2 unchanged by default | Two more fields per codec-aware sidecar | — |
| Special-case `fr_regressor_v3` in C by its id or vocabulary | No sidecar change | Model knowledge in the runtime; breaks on the next model | Couples the runtime to one checkpoint |
| Refuse v3 on the CLI path until a retrain records the range | No inferred value | The shipped model stays unusable through libvmaf | The range is established from the corpus hash and the shared min/max code |
| Clamp the min-max value to [0, 1] | Keeps inputs in the trained range | The trainer does not clamp; a clamped CRF 50 would read as CRF 37 | Silent substitution |

## Consequences

- **Positive**: `fr_regressor_v3` gets the inputs it was trained on; a
  sidecar that declares an encoding libvmaf cannot reproduce fails at load.
- **Negative**: CRF values outside 19..37 are extrapolated by the model, as
  they would be in Python.
- **Neutral / follow-ups**: the one-shot retrain writes the keys from the
  trainer (ADR-1105 retrain scope).

## References

- `Q` (maintainer popup, 2026-10-04, paraphrased): fix every defect found on
  the way; no silent fallback, and a requested option that is not honoured
  fails or the output names the substitution.
- [ADR-0522](0522-tiny-codec-preset-crf-cli-flags.md),
  [ADR-1520](1520-tiny-model-feature-inputs-at-flush.md),
  [ADR-0323](0323-fr-regressor-v3-train-and-register.md).
