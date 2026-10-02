---
paths:
  - core/tools/vmaf.cpp
  - core/tools/test/test_vmaf_frame_readahead.sh
invariant: Reader threads run up to kReadaheadDepth frames ahead; reserve ring slot before pool picture; request_stop both.
---
# Frame read-ahead

- [ADR-1366](../../../docs/adr/1366-cli-frame-readahead.md) — per-input reader
  threads read up to two frames ahead; pool grows by `2 * kReadaheadDepth`.
  See §Frame read-ahead below.

## Frame read-ahead (ADR-1366)

`run_frame_loop()` gives each input `FrameReader`. With read-ahead on,
reader thread runs `fetch_picture()` (pool picture, file read, copy) up to
`kReadaheadDepth` frames ahead; `score_frames()` pops one frame per reader per
step on main thread. Rebase-sensitive rules:

- Reader threads call `fetch_picture()` and `vmaf_picture_unref()`, nothing
  else of libvmaf. `vmaf_read_pictures()`, `classify_frame_fetch()`,
  `release_unpaired_pictures()` and progress line stay on main thread,
  in frame order. Moving any of them onto reader breaks index order and
  ADR-1262 exit semantics.
- reader reserves ring slot (`wait_for_free_slot()`) BEFORE it takes pool
  picture, so it never holds more than `kReadaheadDepth` pictures, and
  `preallocate_cli_pictures()` adds exactly `2 * kReadaheadDepth` when
  `state->readahead`. Fetching first and queueing later makes pool use
  unbounded and lets readers starve libvmaf's pictures.
- `FrameReader` invariants = `assert()` (7: `release_fetched_picture`,
  `start` x2, `wait_for_free_slot`, `publish`, `next`, `request_stop`). Never
  swap for early return / folded condition: broken invariant then = silently
  dropped frame or ended stream. `misc-static-assert` / `cert-dcl03-c` on them
  = false positive of clang-tidy 22 on glibc 2.44 hosts
  (`T-TIDY-GLIBC-244-STATIC-ASSERT-FALSE-POSITIVE-2026-10-02`); CI image
  (glibc 2.39) reports none. Guard:
  `core/test/test_cli_frame_reader_asserts_contract.py`.
- Shutdown calls `request_stop()` on BOTH readers before `join()` on either.
  `request_stop()` drains ring back to pool, which is what wakes
  reader blocked in `vmaf_fetch_preallocated_picture()`; joining one reader
  before stopping other can wait forever on picture queued in other.
- Readers stop after `--frame_cnt` frames (`UINT_MAX` unset) and after
  first frame that ends or fails their stream. `skip_initial_frames()` runs
  inline before readers start.
- `inputs_read_independently()` keeps two handles on one object (same
  `st_dev`/`st_ino` on POSIX; anything but two regular files on Windows) on
  inline path. `--no-reference` opens distorted file twice and therefore
  reads inline on POSIX. Never thread two readers over one pipe: each would
  get whichever frames it reached first.
- Upstream Netflix `vmaf.c` still has inline `for (;;)` fetch loop. sync
  conflict in `run_frame_loop()` resolves to ours; port upstream per-frame read
  changes into `fetch_picture()`, which both paths call.

`core/tools/test/test_vmaf_frame_readahead.sh` (fast suite) pins frame order,
pairing, `--frame_cnt` (no reader reads past it), `--frame_skip_dist`,
inline path, early end and failed read.
