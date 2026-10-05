- The tester image and Windows zip pull-request builds (`Tester Image`,
  `Windows Tester Zip`) start only when a pull request touches their own inputs
  again: their impact selectors are declared `own_paths_only` (ADR-1700), so a
  change to `scripts/ci/`, a required workflow or another CI-wide file no longer
  starts them. A dispatch still builds.
- The amd64 tester image is built and tested every night at 00:29 UTC on
  `master` (ADR-1701); nothing is published. A library change that breaks the
  image shows up as a failed scheduled run of `Publish Tester Image`. See
  `docs/development/release-workflow-verification.md`.
