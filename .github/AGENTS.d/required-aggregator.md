---
paths:
  - .github/workflows/required-aggregator.yml
  - scripts/ci/check-aggregator-names.sh
  - scripts/ci/plan-ci-impact.py
invariant: Aggregator runs on drafts; gates route in-job without paths filters; display names <=30 chars and match aggregator.
---
# Required aggregator, in-job routing, and display name parity

## Required aggregator and draft PRs

[`required-aggregator.yml`](../workflows/required-aggregator.yml) is single
branch-protection status. It must run on draft PRs, fail them explicitly
instead of being skipped. Skipped required context is considered successful
by GitHub branch protection, so job-level draft skips can let auto-merge merge
PR before ready-for-review CI run registers. Aggregator also ignores
check runs older than its current workflow run when selecting sibling
outcomes; otherwise stale draft-era skipped check runs on same commit can
mask real queued or failed ready-for-review checks.

## Required contexts route work in-job (BUG-098)

workflow that hosts aggregator-required context must not use
workflow-level `paths:` or `paths-ignore:`. It starts on pull requests and
master pushes, runs `scripts/ci/plan-ci-impact.py` in unconditional
`impact` job, gates expensive `... work` jobs on selected output, and
always emits exact-name gate job. gate accepts only
`selected=true/work=success` or `selected=false/work=skipped`; planner failure,
cancellation, and any other combination fail. Exact gate names belong in
`strictMustReport` because absence is no longer legitimate path skip.

Keep heavy job names distinct from required gate names, including matrix
fields: otherwise GitHub or aggregator can select wrong same-named
check. `scripts/ci/tests/test_ci_impact.py`, `actionlint`, and
`scripts/ci/check-aggregator-names.sh` pin this structure.

GitHub creates dependent gate's check run only after every `needs` job has
completed. Keep `delayedStrictDependencies` in `required-aggregator.yml`
aligned with every planner/work display name, including every row of matrix
whose aggregate result feeds gate. aggregator uses those checks as
registration proxies and gives gate bounded propagation window; without
that mapping its two-minute missing-check grace can fail while legitimate work
is still running. check-run query must remain paginated: converted
workflows can put full run above API's 100-item page size.

## CI job display names and aggregator parity

All workflow job and matrix display names (`name:`) target $\le 30$ characters,
omit trailing policy citations and redundant parentheticals (see
[`docs/development/ci-job-names.md`](../../docs/development/ci-job-names.md)).
Every required check declared in `required-aggregator.yml` (`const required = [...]`)
is tagged with `# required-aggregator` on its defining `name:` line in its workflow.
Script `scripts/ci/check-aggregator-names.sh` gates 1:1 parity between
`required-aggregator.yml` and workflow files; any rename or addition must
update both atomically.
