- **Go tests that score with the `vmaf` CLI use the build under test.** They
  looked for `vmaf` on `PATH` or under `/usr/local/bin` and so passed or failed
  on whatever release the host had installed. `internal/vmaftest` resolves
  `VMAF_BIN`, else `core/build-cpu/tools/vmaf`, and fails the test, naming the
  build command, when neither exists. See
  [Go development](docs/development/languages.md).
