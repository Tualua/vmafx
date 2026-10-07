---
paths:
  - ai/scripts/mini_retrain.py
  - ai/src/aiutils/pipeline.py
  - ai/src/aiutils/mini_corpus.py
  - ai/src/aiutils/retrain_checks.py
  - ai/e2e/test_mini_retrain_e2e.py
  - .github/workflows/mini-retrain.yml
invariant: The retrain tooling runs end to end in CI; each stage has a manifest; a killed run resumes; NaN fails a gate.
---
<!-- markdownlint-disable MD013 MD060 -->
# Mini retrain and the stage runner

- [ADR-1898](../../docs/adr/1898-mini-retrain-pipeline.md) — **A script the retrain runs is a stage of `mini_retrain.py` with the runbook's flags.** Adding a trainer, exporter or evaluator to `docs/ai/retrain-runbook-1246.md` means adding its stage (inputs, outputs, `stable` set, check) to `build_stages()` and a planted-defect case to `ai/e2e/test_mini_retrain_e2e.py` in the same PR. `aiutils.pipeline.run_pipeline()` resumes only on a `complete` manifest with an unchanged key and intact outputs; never write a stage that updates its input in place (use `Stage.copies`). `aiutils.retrain_checks.gate_verdict()` fails a NaN metric; do not replace it with `plcc < gate`, which passes NaN. `ai/e2e/` is not under the 60 s `ai/tests` limit and is run by the Tiny AI job and nightly.
