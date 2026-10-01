- **actionlint pre-commit hook and Makefile target**: Wired `actionlint`
  pinned to `v1.7.12` (HISS-11 hermetic supply chain pin) into
  `.pre-commit-config.yaml` to validate all 35 GitHub Actions workflow files
  under `.github/workflows/` against `.github/actionlint.yaml`. Added
  `make lint-actions` target and documented workflow linting in
  `docs/development/pre-commit-hooks.md`.
