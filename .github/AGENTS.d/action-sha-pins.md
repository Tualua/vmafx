---
paths:
  - .github/workflows/*.yml
invariant: Every uses directive in workflows must reference 40-character commit SHA with version comment; no floating tags.
---
# SHA-pin invariant for `uses:` directives

Every `uses:` directive in `.github/workflows/*.yml` MUST reference
40-char commit SHA, with original semver tag preserved as
trailing `# vN.M.K` comment. Floating-tag references (`@v4`,
`@release/v1`) trip OSSF Scorecard `Pinned-Dependencies` check,
rejected by sync gate below.

**No exception** (ADR-1356). Org + repo set `sha_pinning_required`;
GitHub enforces it on actions nested inside called reusable workflows
too. Third-party reusable workflow that calls own actions by tag cannot
run here, whatever caller pins. Former carve-out
`slsa-framework/slsa-github-generator` failed so on v1.0.0-rc.2; replaced
by SHA-pinned `actions/attest-build-provenance`. Release workflows:
`scripts/release/tests/test-publication-environment-binding.sh` rejects
unpinned `uses:`.

**Sync gate** (run before merging any `/sync-upstream` that touches
`.github/workflows/`):

```bash
grep -hnE '^\s*(- )?uses:\s+[^@]+@[^ #]+\s*$' .github/workflows/*.yml \
  | grep -vE '@[a-f0-9]{40}'
# Empty output = clean. Anything that prints needs to be SHA-pinned
# before the sync PR can merge.
```

**Resolution recipe** when adding new action or bumping existing
pin:

```bash
# Lightweight tag (most actions):
gh api repos/<owner>/<repo>/git/ref/tags/<vN.M.K> --jq '.object.sha'
# Annotated tag (e.g. github/codeql-action, ilammy/msvc-dev-cmd,
# pypa/gh-action-pypi-publish) — first call returns
# `object.type == "tag"`; dereference it:
gh api repos/<owner>/<repo>/git/tags/<sha-from-prev> --jq '.object.sha'
```

See [ADR-1247](../../docs/adr/1247-scorecard-exact-head-gates.md) for
current project-level Scorecard policy (superseding PR #337 / ADR-0263)
and entry 0231 of [`docs/rebase-notes.md`](../../docs/rebase-notes.md) for standing
re-test command.
