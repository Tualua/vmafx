- Composite actions under `.github/actions/` are now checked in pre-commit, CI and
  `make lint-actions`: the GitHub action schema (`check-github-actions`) and, through
  `scripts/ci/check_composite_actions.py`, their structure and shellcheck of every
  `run:` block. actionlint reads workflows only
  (`docs/development/pre-commit-hooks.md`, "Composite actions").
