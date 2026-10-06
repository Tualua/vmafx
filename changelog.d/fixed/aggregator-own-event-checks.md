- **The master `Required Checks Aggregator` judges the push, not the pull
  request on the same commit.** A pull request the merge train lands by
  fast-forward shares its head commit with the master push, and its cancelled
  runs of the pull-request-only gates counted as failures of every master push.
  Outside a pull request the aggregator now leaves out check runs of
  pull-request workflow runs (`docs/development/ci.md`).
