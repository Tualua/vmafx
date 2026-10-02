---
paths:
  - scripts/ci/check-issue-reference-provenance.py
  - scripts/ci/tests/test_issue_reference_provenance.py
invariant: Protected historical contexts keep their archived `lusoris/vmaf` identity; contracts stay context-scoped.
---
<!-- markdownlint-disable MD013 MD060 -->
# Historical issue-reference provenance (BUG-048 Section E)

`check-issue-reference-provenance.py` protects the small set of historical
issue and pull-request contexts proven to belong to the retired
`lusoris/vmaf` tracker. The active `VMAFx/vmafx` repository reused those
numbers for unrelated pull requests, so restoring a bare issue number inside
one of the protected contexts silently changes the cited object. Keep the
checker, its unit suite, the always-run pre-commit hook, and the Rule
Enforcement self-test wired together.

The contracts are intentionally context-scoped: ordinary bare issue and PR
references normally mean the active fork and must remain allowed, while the
`Netflix/vmaf` tracker is a separate upstream namespace. Add a contract only
after Git history proves the archived identity. Each contract uses a stable
prose anchor and a logical Markdown block; do not replace that with line
numbers, exact whitespace, a repository-wide bare-reference ban, or a network
lookup. See
[Research-2089](../../../docs/research/2089-archived-issue-reference-provenance.md).
