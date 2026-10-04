<!-- markdownlint-disable MD013 MD060 -->
# `scripts/ci/` — agent invariants

Parent: [../AGENTS.md](../../AGENTS.md).

Fork-local CI utilities invoked from `.github/workflows/*.yml`.
Upstream Netflix/vmaf has no equivalent tree; rebase risk =
workflow drift, not merge conflict.

## Rebase-sensitive surfaces

Workflow coupling: rename or signature change must land with
matching update in same PR. Required-status-check names derive
from workflow `name:`; dropped or renamed check blocks every PR
until master fixed.

## When updating from upstream

Fork-introduced; nothing merges from upstream. Risk on
`/sync-upstream`: upstream change to feature extractor emitted-metric
names silently invalidates `FEATURE_METRICS`. Re-run matrix gate
after upstream sync touching `core/src/feature/`.

## CI impact planner (ADR-1140)

- Any file under `scripts/ci/` = CI-authority input: changing one forces
  `full` mode for that PR by design.
