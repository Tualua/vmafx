---
paths:
  - renovate.json
  - scripts/ci/tests/test_renovate_file_patterns.py
  - docs/development/dependency-bot.md
invariant: Mend Renovate config in renovate.json; dependabot.yml disabled; RENOVATE_TOKEN repository secret.
---
# Renovate dependency-update bot (ADR-0363)

## Dependency-update bot: Renovate, not Dependabot (ADR-0363)

Fork uses **Mend Renovate** self-hosted via
[`workflows/renovate.yml`](../workflows/renovate.yml). `.github/dependabot.yml`
has been removed, content archived as `.github/dependabot.yml.disabled`.

On upstream sync:

- If Netflix adds `dependabot.yml`, **never** restore it — merge content
  into `dependabot.yml.disabled` for reference only. Fork's dependency-update
  bot is Renovate; running both simultaneously causes duplicate PRs.
- `renovate.yml` and `renovate.json` are fork-local; Netflix upstream will never
  ship them. They are safe from upstream conflicts.
- `RENOVATE_TOKEN` is repository secret; not committed anywhere. Operator
  playbook is at
  [`docs/development/dependency-bot.md`](../../docs/development/dependency-bot.md).

## Renovate (ADR-0363) supersedes Dependabot

Note: pin updates to `codeql-action/upload-sarif` now arrive via Renovate
(grouped with other GitHub Actions minor+patch bumps), not Dependabot.
