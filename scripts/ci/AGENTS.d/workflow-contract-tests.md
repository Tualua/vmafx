---
paths:
  - scripts/ci/test_e2e_runtime_contract.py
  - scripts/ci/check-helm-selector-isolation.py
  - scripts/ci/tests/test_check_helm_selector_isolation.py
  - scripts/ci/test_security_workflow_contract.py
  - scripts/ci/tests/test-dedupe-gate.sh
invariant: Each contract test pins one workflow step; script, step name and callers change in the same PR.
---
<!-- markdownlint-disable MD013 MD060 -->
# Workflow contract tests: E2E, Helm selectors, Security Scans, dedupe gate

## Workflow coupling

| Script | Workflow lane(s) that invoke it | What couples them |
| --- | --- | --- |
| `test_e2e_runtime_contract.py` | `rule-enforcement.yml` and `e2e-k8s.yml` — `Verify E2E runtime contract` | The always-on PR gate and exact E2E lane enforce explicit CPU node + Go server targets, three-image transfer into kind, exact-local Helm pulls, and the real chart-backed scoring case. Keep it outside the E2E trigger gate as well as inside the image job. |
| `check-helm-selector-isolation.py` | `helm-chart.yml` — `Workload selector isolation` | Positional `helm template` output files; exit 0 isolated, 1 overlap or unevaluable selector, 2 unreadable input. Step runs `tests/test_check_helm_selector_isolation.py` first, then renders Deployment, StatefulSet, Job workloads with operator, node, PDBs on (ADR-1353). |

| Script | Workflow lane(s) that invoke it | What couples them |
| --- | --- | --- |
| `test_security_workflow_contract.py` | `rule-enforcement.yml` — `Verify Security Scans concurrency contract` | The Security Scans group must include workflow, event name, and ref. This keeps same-event cancellation while preventing a schedule on `refs/heads/master` from canceling a master-push CodeQL run (or vice versa). The same test pins C/C++ Meson configure before CodeQL initialization, compile after initialization, and an external `${{ runner.temp }}/build` root so generated compiler probes are never extracted as repository source. |

| Script | Workflow lane(s) that invoke it | What couples them |
| --- | --- | --- |
| `tests/test-dedupe-gate.sh` | `standards-gate.yml` — `Reject duplicate implementation families`; `rule-enforcement.yml` — `Verify duplicate implementation gate`; `.pre-commit-config.yaml` — `dedupe-gate-contract`; `lefthook.yml`; `make verify-all` | The clone scan stays explicit in the required Standards job, both blocking local lefthook stages, and the aggregate local command. Its real-Make fixture proves a scanner failure makes `make verify-all` fail. `standardsctl audit` is not a substitute because it does not run the AST clone detector. |
