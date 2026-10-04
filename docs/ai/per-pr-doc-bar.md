# Tiny-AI Per-PR Doc Bar

A PR that touches the tiny-AI surface (`ai/`, `core/src/dnn/`, anything that
carries a model card) must ship the documentation below in the same PR.
[ADR-0042](../adr/0042-tinyai-docs-required-per-pr.md) sets this 5-point bar, a
specialisation of the project-wide
[ADR-0100](../adr/0100-project-wide-doc-substance-rule.md) doc-substance rule.

The bar co-exists with, and does not replace, the project-wide
documentation rule (rule 7 of the
[agent hard rules](../development/agent-hard-rules.md)). A PR that touches both
a tiny-AI surface and another surface satisfies both bars independently.

## Checklist

Before opening a tiny-AI PR, make sure it contains (paraphrased from ADR-0042,
which has the authoritative wording):

1. A model card under [`docs/ai/models/`](models/), or an updated training-data,
   inference or quantisation page, that explains how a human should use the
   changed artefact.
2. A reproducer command in the PR description: the training or export command
   that can reproduce the artefact.
3. A PLCC / SROCC / RMSE reading on a held-out fold that justifies promotion, or
   an explicit `smoke: true` or deferred status when no promotion is claimed.
4. An ONNX operator-allowlist or runtime compatibility check when a graph
   changes.
5. A CHANGELOG fragment under `changelog.d/added/` or `changelog.d/changed/`.

ADRs and code comments do not substitute for the user-facing model card.

## Applies to

- Model files under `model/tiny/`.
- Model cards under `docs/ai/models/`.
- Training, export, quantisation or calibration scripts under `ai/`.
- Runtime inference paths under `core/src/dnn/`.
- Registry, model-verification or tiny-device selector changes.

## See also

- [ADR-0042](../adr/0042-tinyai-docs-required-per-pr.md): the policy itself.
- [ADR-0100](../adr/0100-project-wide-doc-substance-rule.md): the project-wide
  rule this specialises.
- [overview.md](overview.md): tiny-AI surface overview.
