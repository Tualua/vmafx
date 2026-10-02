<!-- markdownlint-disable MD013 MD060 -->
# `scripts/ci/` — agent invariants

Parent: [../AGENTS.md](../../AGENTS.md).

Fork-local CI utilities. Anything here invoked from
`.github/workflows/*.yml` (see "Rebase-sensitive surfaces" below);
upstream Netflix/vmaf has no equivalent tree, so rebase risk =
"workflow drift", not "merge conflict".

## Rebase-sensitive surfaces

### Workflow coupling

Following pairs tightly coupled — rename or signature
change in one **must** land alongside matching update in
other, in **same PR**. Required-status-check names derive
from workflow file's `name:` fields, so check dropped
or renamed turns into phantom-required gate that blocks every PR
until master fixed.

## When updating from upstream

`scripts/ci/` fork-introduced; nothing here merges from
upstream. Risk on `/sync-upstream` = opposite: upstream
change to feature extractor's emitted-metric names would silently
invalidate `FEATURE_METRICS` rows. Re-run matrix gate after any
upstream sync touching `core/src/feature/`.

## CI impact planner (ADR-1140)

- Any file under `scripts/ci/` = CI-authority input: changing one forces
  `full` mode for that PR by design.
