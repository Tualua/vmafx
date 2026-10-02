---
paths:
  - .github/workflows/go-ci.yml
  - scripts/ci/test_go_workflow_contract.py
invariant: go vet + go test required; go fix -diff first step; non-draft trigger with documentation-only bypass.
---
# Go validation (ADRs 1238 and 1338)

`go-ci.yml` reports `go vet + go test` as required. It starts on non-draft
PRs including `ready_for_review`, master pushes, and manual dispatches,
then gates heavyweight steps on `go_checks` (`go` plus `c_core`). Preserve
its explicit documentation-only no-work result, CPU/optional-backend
settings, and CI-authority classification. Rules job runs
`scripts/ci/test_go_workflow_contract.py` before authoring exemptions;
this test executes aggregator script with failing Go outcomes. first
selected source gate after `setup-go` is exactly `go fix -diff ./...`; keep it
before native dependency installation/build, non-mutating, and under same
impact predicate. Local `go-fix` and `go-fix-check` Make targets must stay
aligned with that command.
