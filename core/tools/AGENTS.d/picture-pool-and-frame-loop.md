---
paths:
  - core/tools/vmaf.cpp
  - core/tools/test/test_vmaf_read_error_exit.sh
invariant: Picture pool is always-on; classify_frame_fetch() tests error before end of stream; read failures exit 102.
---
# Picture pool, fetch cleanup, and read error exit semantics

- **`--frame_skip_ref` / `--frame_skip_dist`** pre-loops in
  [vmaf.cpp](../vmaf.cpp) MUST `vmaf_picture_unref()` each fetched picture
  immediately. Picture pool is always-on (see ADR-0104 below) and
  fixed-size; without unref pool exhausts after N skips, next
  fetch blocks indefinitely. Re-test with
  `python -m pytest python/test/command_line_test.py
  ::VmafexecCommandLineTest::test_run_vmafexec_with_frame_skipping` — if
  it hangs (timeout, no output), unref is missing or wrong.
- **EOF / read-error cleanup in `fetch_picture()` is load-bearing.**
  `fetch_picture()` reserves pooled `VmafPicture` before asking
  input reader for bytes. If reader returns EOF or error,
  reserved picture MUST be unrefed before `fetch_picture()` returns
  `1` (EOF) or `-1` (error). `run_frame_loop()` must also unref
  opposite picture when only one side read successfully. Otherwise
  CLI can finish writing output, then hang forever in `vmaf_close()`
  while picture pool waits for leaked unread slot.

- [ADR-0104](../../../docs/adr/0104-picture-pool-always-on.md) — picture
  pool is always compiled in and sized for live-picture set; this
  is what makes `--frame_skip_*` unref invariant load-bearing.

## Read failures exit 102, short streams exit 0 (ADR-1262)

`run_frame_loop()` returns `FrameLoopResult { frames, exit_code }`, not bare
count. Returning only count is why `vmaf` used to exit 0 on every read
failure and still write report over truncated prefix.

`classify_frame_fetch()` tests error **before** end of stream. Order is
load-bearing: `fetch_picture()` gives `1` at EOF, `-1` on error, so
`ret1 && ret2` is true when both sides FAIL. Testing it first classifies two
corrupt inputs as clean end of stream — silent, exit 0. Upstream still has that
order (Netflix/vmaf#1604, known, unfixed), so rebase conflict offers it as
"theirs". Keep ours.

stream that ENDS earlier than partner is not error: keeps `ended before`
warning, keeps report, exits 0. Scoring common prefix of shorter clip is
supported use. Do not fold two cases together.

`core/tools/test/test_vmaf_read_error_exit.sh` pins all four cases, `fast` suite.
