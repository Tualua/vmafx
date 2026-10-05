- **Python tests that run the `vmaf` CLI use the build under test.** Several
  suites carried their own lookups, and some fell back to the `vmaf` on `PATH`
  or under `/usr/local/bin`, so they passed against a stale host install (the
  CHUG smoke test, the vmaf-tune fast-path parity test and the MCP golden-pair
  smoke test all ran `/usr/local/bin/vmaf` 3.2.0 on a host without a build).
  `scripts/lib/vmaftest.py` is now the one resolver: `VMAF_BIN`, then
  `VMAF_BIN_FOR_TESTS`, then `build/`, `core/build/` and `core/build-cpu/`. A
  variable that names no executable is an error, and with nothing built the
  tests skip with a message the CI jobs fail on. See
  [how a test finds the vmaf binary](docs/development/test-suites.md#how-a-test-finds-the-vmaf-binary).
