---
paths:
  - .github/workflows/rule-enforcement.yml
  - .github/PULL_REQUEST_TEMPLATE.md
  - scripts/ci/test_fail_closed_ci.py
invariant: Only deep-dive-checklist blocks; opt-out regex and port exemption stay in sync; advisory paths mirror ADRs.
---
# Automated rule enforcement workflow invariants (ADR-0124)

## Rule-enforcement split (ADR-0124)

[`rule-enforcement.yml`](../workflows/rule-enforcement.yml) has three
jobs. Only **one** is allowed to be required-status-check-blocking:

- `deep-dive-checklist` — **blocking**. Predicate is mechanically
  decidable (ticked checkboxes + referenced files in diff).
- `doc-substance-check` — **advisory** (`continue-on-error: true`).
  Predicate needs "is this pure refactor?" judgement.
- `adr-backfill-check` — **advisory** (`continue-on-error: true`).
  Predicate needs "is this decision non-trivial?" judgement.

Advisory/blocking split is load-bearing — see
[ADR-0124](../../docs/adr/0124-automated-rule-enforcement.md) §Consequences
and VIF-fix false-positive in
[Research-0002](../../docs/research/0002-automated-rule-enforcement.md)
§"Dead ends". Moving either advisory job into `required_status_checks`
(or flipping its `continue-on-error` flag) is policy change, needs
superseding ADR.

## Fail-closed test and scan outcomes

`scripts/ci/test_fail_closed_ci.py` runs in blocking
`deep-dive-checklist` job and through `fail-closed-ci-contract` local hook.
Keep both callers. Test, coverage, benchmark, scan, and test-discovery commands
must expose their real exit status. step may use `continue-on-error` only to
collect diagnostics when later `if: always()` step checks its raw
`steps.<id>.outcome` and fails job. advisory job or step may remain
non-blocking when existing ADR says so, but its command must not append
`|| true` and erase failure outcome.

## Opt-out syntax parser

`deep-dive-checklist` job parses PR bodies for ADR-0108's
opt-out lines:

```text
no digest needed: <reason>
no alternatives: <reason>
no rebase-sensitive invariants
no reproducer needed: <reason>
no changelog needed: <reason>
no rebase impact: <reason>
```

Regex is intentionally loose on wording. If
[`PULL_REQUEST_TEMPLATE.md`](../PULL_REQUEST_TEMPLATE.md) ever renames
six deliverables, parser's `key` mapping in
`rule-enforcement.yml` (step "Parse six-deliverable checklist")
must move in lockstep. Search for `case "${item}" in` block.

## Upstream-port exemption

`deep-dive-checklist` skips when PR title starts with `port:` /
`port(scope):` or branch name starts with `port/`. Those are
only two knobs; port PR using neither form WILL be
blocked. If sync skill
([`.claude/skills/port-upstream-commit/`](../../.claude/skills/port-upstream-commit/))
ever changes its branch-naming or title convention, update
workflow's `Skip upstream-port PRs` step.

## ADR collision guard and stacked PRs

`adr-collision-check` job in
[`rule-enforcement.yml`](../workflows/rule-enforcement.yml) scans open PRs for
ADR-number collisions, but must skip descendant stacked PRs whose
`baseRefName` chain reaches current PR's `head.ref`. Those descendants
intentionally contain parent PR's ADR files while merge train waits.
Never replace that base-branch-chain check with flat "any open PR with
same number fails" scan; it deadlocks ADR-bearing parent PRs whenever draft
children are queued.

Phase 1 compares added ADR numbers against PR's event `base.sha`, not
live `origin/master`. That distinction is load-bearing: if PR merges before
collision job starts, live `origin/master` already contains PR's own
ADR, produces false self-collision.

## Advisory surface-path lists

Both advisory jobs grep diff for specific path prefixes
(`core/include/`, `meson_options.*`, `mcp-server/`, etc.).
These mirror
[ADR-0100](../../docs/adr/0100-project-wide-doc-substance-rule.md) §Per-surface
and ADR-policy-surface list from
[ADR-0106](../../docs/adr/0106-adr-maintenance-rule.md). When either
ADR adds new user-discoverable or policy-surface path, update
grep patterns in `rule-enforcement.yml` in same PR — otherwise
advisory goes silent on new surface.
