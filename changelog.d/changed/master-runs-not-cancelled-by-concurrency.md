- CI: a push to `master` no longer cancels the workflow runs of the previous
  master commit. The concurrency group of every push-to-master workflow carries
  the commit SHA on master and `cancel-in-progress` stays on for pull request
  refs; publish, release, Scorecard and Pages deploy stay serialised. A contract
  test (`scripts/ci/tests/test_master_concurrency_contract.py`) enforces it
  (ADR-1673).
