<!-- markdownlint-disable MD013 MD060 -->
# ADR-1570: Tiny models trained on restricted data stay, their cards quote the data's terms, and RC9 retrains them on cleared data

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: license, compliance, tiny-ai, docs, fork-local

## Context

The licence audit of the published artifacts
([Research-2140](../research/2140-production-artifact-licence-audit.md), gap 15,
`T-PROD-LICENCE-MODEL-TRAINING-DATA-2026-10-04`) found that shipped tiny models
were trained on data whose own terms limit its use: the Netflix Public Dataset
(`fr_regressor_v1` to `v3`, `vmaf_tiny_v1` to `v4`), KoNViD-1k
(`nr_metric_v1`, `learned_filter_v1`, `vmaf_tiny_v2` to `v4`), BVI-DVC
(`vmaf_tiny_v2` to `v4`), DUTS-TR, whose images are ImageNet's
(`saliency_student_v1`, `v2`), and torchvision's ImageNet-trained SqueezeNet
features (`lpips_sq_v1`). The cards described these terms in their own words,
and two descriptions had no source: `fr_regressor_v1` said Netflix's licence
"forbids redistribution", and the saliency cards called DUTS "free for academic
and research purposes". Neither appears on the dataset pages read on
2026-10-04. The weights themselves are BSD-2-Clause-Patent (LPIPS:
its upstream licences) on the reading that fitted parameters are not a copy of
the data.

## Decision

The models stay in the release under their recorded licences. Each affected
model card gains a "Training data terms" section that quotes the terms of
every dataset the model was trained on verbatim, as the dataset states them,
names a research-only limit where the dataset sets one (ImageNet, BVI-DVC
source 15, KoNViD-1k's offer to the research community), states the fork's
reading as the fork's and not as a permission, and points to the RC9 retrain.
`docs/ai/training-data.md#dataset-terms` holds the canonical quotes with their
sources; `scripts/ci/tests/test_model_card_dataset_terms.py` fails when a card
lacks the section or a quote differs from the canonical text. The two
unsourced descriptions are replaced by the quotes. The retrain on data cleared
for redistribution belongs to RC9, the one-shot real retrain of
[ADR-1490](1490-rc3-rc9-candidate-map-cpu-capability.md)
(`T-TINY-AI-RETRAIN-CLEARED-DATA-2026-10-04`).

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Keep the models, state the terms, retrain in RC9 (chosen) | The release keeps working models; users see the data's own words and can judge; the retrain lands with the planned training candidate | The weights stay on the fork's reading until RC9 | Maintainer's choice |
| Drop the affected models now | No reliance on a reading | Removes the default tiny models (`vmaf_tiny_v3`, the saliency student, the NR baseline) and breaks every user of them before a replacement exists | Too disruptive for a weak finding |
| Retrain on cleared data now | Clean weights in RC3 | Training is RC9 work under ADR-1490; cleared data with MOS labels has to be found first | Against the candidate map |
| Paraphrase the terms in each card | Shorter cards | The paraphrases were already wrong twice | Quotes are checkable, paraphrases are not |

## Consequences

- **Positive**: every affected card carries the data's terms in the data's
  words, a test keeps the cards and the canonical page in step, and the
  retrain has a row and a candidate.
- **Negative**: the cards are longer; the BVI-DVC notice is quoted in full in
  three cards.
- **Neutral / follow-ups**: RC9 closes `T-TINY-AI-RETRAIN-CLEARED-DATA-2026-10-04`
  by retraining these models on data cleared for redistribution and updating
  their cards and registry entries; a new model trained on any dataset gets
  the section and an entry in the test's card list.

## References

- `Q` (popup 2026-10-04, training data): "Keep, state terms, retrain RC9 (Recommended)".
- [ADR-1490](1490-rc3-rc9-candidate-map-cpu-capability.md), [ADR-1513](1513-production-artifact-licensing.md), [Research-2140](../research/2140-production-artifact-licence-audit.md).
- Dataset pages read 2026-10-04: Netflix/vmaf `resource/doc/datasets.md` at `0fb4152418d0351901e9c5fd2d30668dced89cdb`; <http://database.mmsp-kn.de/konvid-1k-database.html>; <https://fan-aaron-zhang.github.io/assets/copyrights/BVI-DVC.txt>; <http://saliencydetection.net/duts/>; <https://image-net.org/download.php>; <https://docs.pytorch.org/vision/stable/models.html> (torchvision 0.29).
