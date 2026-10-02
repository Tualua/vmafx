---
paths:
  - scripts/ci/check-aggregator-names.sh
  - scripts/ci/tests/test-check-aggregator-names.sh
  - scripts/ci/required_aggregator_harness.py
  - scripts/ci/test_go_workflow_contract.py
invariant: `required` list = `# required-aggregator` markers; one reporter per required name; contract suites share one harness.
---
<!-- markdownlint-disable MD013 MD060 -->
# Required aggregator: check names and the shared harness

## check-aggregator-names.sh invariants

- Gates 1:1 parity between required status checks declared in
  `.github/workflows/required-aggregator.yml` (`const required = [...]`) and
  `# required-aggregator` markers on `name:` fields across workflow files.
- Enforced locally via `make lint-sh` and pre-commit hook `check-aggregator-names`.
- Display names must stay concise ($\le 30$ chars) per `docs/development/ci-job-names.md`.

## Workflow coupling

| Script | Workflow lane(s) that invoke it | What couples them |
| --- | --- | --- |
| `check-aggregator-names.sh` | `check-aggregator-names` pre-commit hook; `rule-enforcement.yml` — `Required check names have one reporter each (ADR-1259)` (gate + `tests/test-check-aggregator-names.sh`) | Two invariants: the aggregator's `required` list equals the `# required-aggregator`-marked names, and each required name is reported by exactly one job (the aggregator keeps only the newest run per name, so a shared name lets one job mask the other's failure — `Windows MSVC+CUDA`, fixed 2026-09-19). `job_names()` skips everything under a `steps:` key and the workflow's top-level `name:`; change that parser and the fixture test together. A new lane must not reuse a required name. |

| Script | Workflow lane(s) that invoke it | What couples them |
| --- | --- | --- |
| `required_aggregator_harness.py` | Shared by `test_go_workflow_contract.py` and `test_sycl_tidy_workflow_contract.py` | Owns the one Node.js driver for executing the embedded aggregator against synthetic check results. Keep both contract suites on this harness so polling-time simulation and result decoding cannot drift. |
| `test_go_workflow_contract.py` | `rule-enforcement.yml` — `Verify Go required-check contract` | Uses the shared aggregator harness for Go pass/fail outcomes; guards ready-event coverage and step-level `go_checks` routing; pins the early, non-mutating `go fix -diff ./...` gate and matching Make targets. Keep it before authoring exemptions (ADRs 1238 and 1338). |
