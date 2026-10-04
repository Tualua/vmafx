<!-- markdownlint-disable MD013 MD060 -->
# ADR-1520: Feature-vector tiny models request their inputs, score at flush, and fail on a missing input

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: ai, dnn, tiny-ai, api, correctness, fork-local

## Context

A feature-vector tiny model (`vmaf_tiny_v1`..`v4`, `fr_regressor_v1`..`v3`)
reads libvmaf features from the feature collector. Before this decision the
attach path registered no extractor, the model ran inside
`vmaf_read_pictures()`, and `dnn_lookup_feature()` returned `0.0` for any
feature the collector did not hold. With the default `vmaf_v1.0.16_3d0h` model
the run computes none of the canonical-6 under their default-option keys, so
`vmaf_tiny_v2` printed -0.853302 for every frame of the Netflix 576x324 pair.
Even with `--feature adm --feature vif --feature motion` the result was wrong
after frame 0: `integer_motion` writes `motion2` only in its `flush()`, so every
read-time evaluation saw `motion2 = 0` (`fr_regressor_v1` frame 1: 75.62 at read
time, 81.10 on the finished features).

The codec block of the codec-aware models had the same flaw. The loader set
the third-from-last slot to 1 and left `preset_norm` and `crf_norm` at 0. For
`fr_regressor_v2` that slot is `unknown`, but `crf_norm = 0` means CRF 0, which
no row of the training corpus has. For `fr_regressor_v3` the slot is
`hevc_videotoolbox` (its vocabulary has no `unknown`), and
`vmaf_dnn_codec_block_fill(NULL)` set the last slot, also `hevc_videotoolbox`.
The `fr_regressor_v2_ensemble_v1_seed*` sidecars describe a 14-wide block while
their graphs take 6 inputs, and the loader fed them slot 3 regardless.

The maintainer decided on 2026-10-04 that such a model must never score a
silent number: the run computes the features the sidecar names, or the model
fails with an error naming what is missing.

## Decision

We will make a feature-vector model own its inputs:

1. Attach resolves every input slot to an extractor (sidecar `feature_order` /
   `features`; canonical-6 only for a six-wide model without a list) and
   registers it with default options. A name no extractor writes fails with
   `-EINVAL`; a list of another length than the input fails with `-ENOTSUP`.
2. The model runs in `flush_context()` after every backend flush, over frames
   0 to the last index read. A frame with some but not all inputs fails the
   flush with `-EINVAL` and a log line per missing feature; a frame with none
   (a skipped index) and a frame `n_subsample` drops are not scored. A cursor
   lets a retried flush resume.
3. A second input is accepted only when the sidecar's `encoder_vocab` plus two
   equals its width. Its block starts zero and the model refuses to score
   (`vmaf_read_pictures()` returns `-EINVAL` on the first frame) until
   `vmaf_dnn_set_codec_context()` succeeds. That function finds `"unknown"` by
   name and fails for a vocabulary without it. The CLI requires `--tiny-crf`
   with `--tiny-codec` / `--tiny-preset`.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Request inputs, score at flush (chosen) | Correct on every frame, `motion2` included; the default CLI run works with no extra flag; works with GPU twins and the thread pool, whose features also arrive after the read | Tiny scores exist only after the flush; an API caller polling per-frame scores during the read sees none for the model | — |
| Request inputs, keep scoring at read time | Per-frame scores during the read | `motion2` of frame n exists only after the motion extractor's flush; every frame would fail, or read a missing value | Wrong by construction |
| Fail at attach unless the caller registered the inputs | No hidden extractor registration | The default CLI run fails; option-suffixed keys (`adm` with `egl=1` in the default model) look present but are other features | The maintainer preferred computing the inputs when the sidecar names them |
| Keep the codec seed, document it | No behaviour change for `fr_regressor_v2` users | CRF 0 and, for v3, a real encoder's one-hot are inputs the run never had | Contradicts the no-silent-number rule |
| Seed the codec block with the trainer's missing-field defaults (`unknown`, `medium`, CRF 23) | `fr_regressor_v2` runs without flags | Per-trainer knowledge in C; v3's trainer normalises CRF with corpus bounds and has no `unknown` | Only covers one trainer, still a guessed CRF |

## Consequences

- **Positive**: the CLI default run scores feature-vector models on their
  real inputs; no missing input becomes `0.0`; a broken sidecar fails at load.
- **Negative**: tiny scores appear only after the flush. Codec-aware models
  need `--tiny-codec` and `--tiny-crf`, and the ensemble seeds do not load
  until their sidecars describe the 6-wide block their graphs take. The
  extra extractors add their cost to a run that did not compute them before.
- **Neutral / follow-ups**: the public API behaviour of `vmaf_use_tiny_model()`
  and `vmaf_dnn_set_codec_context()` changes (documented in
  `core/include/libvmaf/dnn.h`, [the DNN API page](../api/dnn.md) and
  [inference](../ai/inference.md)); the ensemble sidecars and
  `fr_regressor_v3`'s CRF normalisation are separate rows in
  [`docs/state.md`](../state.md).

## References

- `Q` (maintainer popup, 2026-10-04, paraphrased): track every docs-audit
  defect and fix it now; a feature-vector tiny model whose features were not
  computed must not score a zero vector, the run computes the features the
  sidecar names or the model fails naming them.
- Docs-audit defect 23 (`core/src/libvmaf.c` feature-vector path).
- [ADR-0518](0518-tiny-model-loader-external-data-and-feature-rank.md)
  (rank-2 feature-vector loader),
  [ADR-0522](0522-tiny-codec-preset-crf-cli-flags.md) (codec block and CLI
  flags), [ADR-0524](0524-tiny-model-loader-symbolic-batch-dim.md),
  [ADR-0244](0244-vmaf-tiny-v2.md).
