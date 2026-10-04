- **`make test-affected BASE=<sha> HEAD=<sha>` runs the Python test suites a
  change touches, locally, in cached hash-locked environments.**
  It maps the changed files to the suites of `.github/test-suites.json`, builds
  each suite's virtual environment from its lock files once, and fails on a test
  failure, a time-cap overrun or a skip for a missing dependency or input. See
  [Run the affected suites locally](docs/development/test-suites.md#run-the-affected-suites-locally).
