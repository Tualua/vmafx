---
paths:
  - scripts/ci/state-md-touch-check.sh
  - scripts/ci/test-state-md-touch-check.sh
invariant: Trigger predicate, opt-out sentinel and placeholder vocabulary couple to the Bug-status row of the PR template.
---
<!-- markdownlint-disable MD013 MD060 -->
# `docs/state.md` touch check

## Workflow coupling

| Script | Workflow lane(s) that invoke it | What couples them |
| --- | --- | --- |
| `state-md-touch-check.sh` | `rule-enforcement.yml` — `state-md-touch-check` job (ADR-0334) | Reads `$PR_TITLE`, `$PR_BODY`, `$BASE_SHA`, `$HEAD_SHA` from the workflow env. The trigger predicate (Conventional-Commit `fix:`, bare `bug` token, GitHub close-keywords, unchecked Bug-status checkbox) and opt-out sentinel (`no state delta: REASON`) are coupled to the `## Bug-status hygiene` row in `.github/PULL_REQUEST_TEMPLATE.md` — keep them in sync. |
| `state-md-touch-check.sh` | `rule-enforcement.yml` — `state-md-touch-check` job (ADR-0334) | Reads `$PR_TITLE`, `$PR_BODY`, `$BASE_SHA`, `$HEAD_SHA` from the workflow env. The trigger predicate (Conventional-Commit `fix:`, bare `bug` token, GitHub close-keywords, unchecked Bug-status checkbox) and opt-out sentinel (`no state delta: REASON`) are coupled to the `## Bug-status hygiene` row in `.github/PULL_REQUEST_TEMPLATE.md` — keep them in sync. The placeholder-ref hardening (ADR-0334 status update 2026-05-09) additionally rejects inserted lines in `docs/state.md` matching `this PR` / `this commit` / `TBD` / `<PR>` / `#NNN`; the placeholder vocabulary is coupled to PR #541's audit findings. |
| `test-state-md-touch-check.sh` | (local-only fixture driver, not invoked by CI) | Run before pushing changes to `state-md-touch-check.sh`; constructs throw-away `mktemp -d` git repos so the test is hermetic. |
