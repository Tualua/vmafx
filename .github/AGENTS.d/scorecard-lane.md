---
paths:
  - .github/workflows/scorecard.yml
  - scripts/ci/tests/test_scorecard_*.py
invariant: upload-sarif commit SHA resolves; publisher restricted to approved actions; artifact binds run ID and event SHA.
---
# OSSF Scorecard pin and report authenticity (ADR-1247)

## OSSF Scorecard pin invariant

`.github/workflows/scorecard.yml` references
`github/codeql-action/upload-sarif@<sha>`. Preserve resolving immutable
upstream commit and publishing restrictions of pinned Scorecard action.
Historical unresolved-pin failure is recorded in Research-0053; it does not
establish that later tag move invalidates otherwise valid commit. Whenever
this pin is updated (Renovate or manual), verify new SHA resolves:

```bash
pin=$(grep -oE 'codeql-action/upload-sarif@[a-f0-9]{40}' \
      .github/workflows/scorecard.yml | head -1 | cut -d@ -f2)
gh api "/repos/github/codeql-action/commits/$pin" --jq '.sha'
```

422 response here is canary that workflow is about to start
failing on next push. See [ADR-1247](../../docs/adr/1247-scorecard-exact-head-gates.md)
and [Research-0053](../../docs/research/0053-ossf-scorecard-investigation.md).

## Scorecard scope and report authenticity (ADR-1247)

Keep `scorecard.yml` publisher restricted to upstream-approved actions;
only that job may obtain OIDC write permission. Its generated JSON and SARIF
artifact name binds run ID, attempt and event SHA. Separate Master Gate
requires successful analysis, downloads only same run's artifact; never
replace it with public latest-score API. Action scans remote HEAD, so
mismatch with event SHA must fail. Keep PR workflow read-only and
non-publishing, with exact head checkout and source snapshots around all eleven
local file checks. Applicable Scorecard context must report success in
aggregator; absent, skipped and neutral are failures. Inactive event's gate
must not create pre-merge wait for future master result. Contract tests are
`scripts/ci/tests/test_scorecard_*.py`; preserve pinned source/schema checks
and exact no-release-only unavailable case. Both gates and local
`repository-security-contract` hook must run offline ADR-1248 controls.
Master gate runs read-only repository security checker outside
publisher, using built-in token, retaining separate live receipt;
failed Scorecard assessment must not silently skip that drift check.
