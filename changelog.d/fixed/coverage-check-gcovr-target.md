- **`make coverage-check` works.** `make coverage` now builds, tests and reports
  the way the `Coverage Gate` job does (gcovr, atomic counters, serial suite) and
  writes `build-coverage/coverage.json`; `coverage-check` hands that file and the
  local floors (37 % overall, 85 % critical) to `scripts/ci/coverage-check.sh`.
  The target used to pass an lcov `.info` file to a script that reads gcovr JSON,
  so it could never pass. The script refuses a non-gcovr input with exit 2.
  `make coverage` needs `gcovr` instead of `lcov`.
