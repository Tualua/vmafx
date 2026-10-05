---
paths:
  - .github/workflows/scorecard.yml
  - scripts/ci/scorecard_gate.py
  - scripts/ci/tests/test_scorecard_*.py
invariant: upload-sarif commit SHA resolves; publisher restricted to approved actions; artifact binds run ID and event SHA.
---
# OSSF Scorecard pin and report authenticity (ADR-1247)

## OSSF Scorecard pin invariant

`.github/workflows/scorecard.yml` references
`github/codeql-action/upload-sarif@<sha>`. Preserve resolving immutable
upstream commit and publishing restrictions of pinned Scorecard action.
Research-0053 records historical unresolved-pin failure; later tag move alone
never invalidates otherwise valid commit. On every pin
update (Renovate or manual), verify new SHA resolves:

```bash
pin=$(grep -oE 'codeql-action/upload-sarif@[a-f0-9]{40}' \
      .github/workflows/scorecard.yml | head -1 | cut -d@ -f2)
gh api "/repos/github/codeql-action/commits/$pin" --jq '.sha'
```

422 response here = canary: workflow starts failing on next push. See [ADR-1247](../../docs/adr/1247-scorecard-exact-head-gates.md)
and [Research-0053](../../docs/research/0053-ossf-scorecard-investigation.md).

## Scorecard scope and report authenticity (ADR-1247)

Keep `scorecard.yml` publisher restricted to upstream-approved actions;
only publisher job gets OIDC write permission. Publisher's JSON and SARIF
artifact name binds run ID, attempt and event SHA. Separate Master Gate
requires successful analysis, downloads only same run's artifact; never
replace that artifact with public latest-score API. Action scans remote HEAD, so
final master ref decides (ADR-1686): equal to event SHA = verdict; invalid ref
or move to non-descendant = fail; move to descendant (compare `ahead`, event
SHA as base and merge base) = superseded, exit 3, gate cancels own run, newer
commit's push run gives verdict. Superseded never green: no exit 0, no
`continue-on-error`, no skipped gate job. Gate job keeps `actions: write` for
that cancel and job `if: ${{ !cancelled() }}`, never `always()` (GitHub keeps
job with true `if` running through cancel). Keep PR workflow read-only and
non-publishing, with exact head checkout and source snapshots around all eleven
local file checks. Aggregator takes only success from applicable Scorecard
context; absent, skipped and neutral fail. Inactive event's gate never
creates pre-merge wait for future master result. Contract tests:
`scripts/ci/tests/test_scorecard_*.py`; preserve pinned source/schema checks
and exact no-release-only unavailable case. Both gates and local
`repository-security-contract` hook run offline ADR-1248 controls.
Master gate runs read-only repository security checker outside
publisher, using built-in token, retaining separate live receipt;
failed Scorecard assessment never silently skips that drift check.
