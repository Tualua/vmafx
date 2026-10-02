---
paths:
  - core/tools/vmaf_vpl.c
invariant: vpl_decode_frame retries under VPL_DECODE_MAX_ATTEMPTS; VplFallbackState flags release acquired stages.
---
# VPL decode to SYCL pipeline

- `vmaf_vpl.c` — VPL decode -> SYCL pipeline (fork-local, not upstream).
  - **`vpl_decode_frame` retries under `VPL_DECODE_MAX_ATTEMPTS`.**
    ceiling is *derived*: `VPL_SYNC_TIMEOUT_MS` / `VPL_DECODE_RETRY_US`, i.e.
    60 s timeout same function already hands
    `MFXVideoCORE_SyncOperation()` at 1 ms back-off it already used.
    Change one of three macros and other two must still describe
    same wall clock. Never restore bare `for (;;)`, and never widen
    ceiling without re-deriving it from measured busy-loop distribution
    ([ADR-1287](../../../docs/adr/1287-cli-tool-unbounded-loop-ceilings.md)).
  - **`VplFallbackState` flags are load-bearing, not defensive.**
    `vpl_fallback_release()` reads `have_ref_img` / `have_dis_img` /
    `have_ref_map` / `have_dis_map` / `have_ref_pic` / `have_dis_pic` to
    release exactly what acquisition stages managed to take, in order
    retired `cleanup:` label used. Setting flag without matching
    release branch (or vice versa) leaks or double-frees; this is what
    replaced `clang-analyzer-deadcode.DeadStores` NOLINT that used to sit
    on `have_dis_pic`.
