- **The dev, release, git-hook and e2e scripts meet the HISS shell rules
  (ADR-1142).** The git hooks (`pre-push-pr-body-lint.sh`, `pre-rebase`, the
  native `pre-commit.sh`), the release scripts, `sync-pelorus-interop.sh`,
  `bench-multi-resolution.sh`, the e2e score smoke test, the ensemble kit and
  the dev helpers handle the exit status of every command they used to discard
  with `|| true`; their `curl` calls have time limits; `pre-commit.sh`,
  `bench-multi-resolution.sh` and `sync-pelorus-interop.sh` are split so no
  function is over 60 lines; the ensemble kit's sourced platform helper turns on
  strict mode only when run directly. New tests pin the native pre-commit hook
  and the benchmark's ok and skip cells. The HISS baseline loses 56 infractions.
