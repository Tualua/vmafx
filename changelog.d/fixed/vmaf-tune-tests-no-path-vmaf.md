- The vmaf-tune Python tests no longer start the host's `vmaf` through the
  backend probe (a suite-wide fixture keeps the default probe off `PATH`), and
  `go test ./pkg/fast/` runs the vmaf CLI of the build under test
  (`VMAF_BIN` or `core/build-cpu`) instead of `vmaf` on `PATH`.
