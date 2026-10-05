- **Tester report: a failure names its cause
  (`T-TESTER-REPORT-DROPS-FAILURE-CAUSE-2026-10-05`).** A failed `vmaf` run on a
  fixture used to keep only the last line it printed, often a warning printed
  while closing; its `error` now also carries every `problem ...`, `error: ...`
  and libvmaf `ERROR` / `WARNING` line (at most 20 lines, 4 KB) and the signal's
  name for a crash. A unit-test program that is killed by a signal, times out or
  exits with a failure status without reporting a failing case is named in
  `unit_tests.reason` with the case it was in, and that case is recorded as
  `fail` with `no verdict printed: ...`; a timeout keeps the cases printed before
  it. The report schema is unchanged.
