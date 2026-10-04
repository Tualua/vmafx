---
paths:
  - scripts/ci/suite_registry.py
  - scripts/ci/tests/test_suite_registry.py
  - .github/test-suites.json
  - requirements/locks/tooling-tests.in
invariant: Every test file is in one suite with required checks; Tooling Tests runs the registry, not a hand list.
---
<!-- markdownlint-disable MD013 MD060 -->
# Test-suite registry (ADR-1528)

`.github/test-suites.json` maps every tracked test file (the `name_patterns`
and `path_patterns`) to exactly one suite and every suite to the required
checks that run it. `suite_registry.py check` runs first in the `Tooling Tests`
job and as the `suite-registry` pre-commit hook.

- A new test directory goes into a suite's `paths`, or into a new suite whose
  check is in the aggregator's `required` list. Do not add it to `not_tests` to
  make the check pass: `not_tests` is for files that are not tests (drivers,
  dataset modules), each with a reason.
- `run tooling` builds its file list from the registry. Do not replace it with
  a hand-written list of files or directories in the workflow, which is how
  60 script tests went unrun.
- The check fails on a suite path or `not_tests` entry that matches no file, so
  a rename updates the registry in the same commit.
- `_run_pytest` runs pytest in-process: `safe_subprocess` executes the
  resolved interpreter, which would leave the virtual environment that holds
  `requirements/locks/tooling-tests.txt` (pytest-timeout would vanish).
- The tooling lock carries the docs stack, semgrep, reuse and pre-commit
  because tests of the suite import or drive them. Without them the docs tests
  skip, and `test_semgrep_vendored_scope.py` and `scripts/githooks/tests` fail.

Tests: `scripts/ci/tests/test_suite_registry.py` (an unwired file, a file in two
suites, a non-required check, stale entries, duplicate keys, failing Python
and shell tests, a suite the runner cannot run, and the repository itself).
