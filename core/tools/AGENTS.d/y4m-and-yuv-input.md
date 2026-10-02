---
paths:
  - core/tools/y4m_input.c
  - core/tools/yuv_input.c
  - core/tools/vmaf_bench.c
invariant: y4m 411->422jpeg guards secondary write; readers cast to (size_t) before multiply; malloc checks return.
---
# Input reader safety and dimension validation

- **`y4m_convert_411_422jpeg` chroma-row write guards are
  load-bearing** (rebase-sensitive). 4:1:1 → 4:2:2-jpeg upsample
  in [y4m_input.c](../y4m_input.c) writes both even and odd output
  sub-pixels per loop iteration. Destination chroma row width
  `dst_c_w` can be 1 (e.g. width-2 frame: `dst_c_w = (2 + 2 - 1) /
  2 = 1`), in which case writing `_dst[(x << 1) | 1]` = 1-byte
  heap-buffer-overflow. **All three sub-loops** in this routine must
  guard secondary write with `(x << 1 | 1) < dst_c_w`. Upstream
  Daala / Netflix carry same code shape; if `/sync-upstream`
  reintroduces unguarded write, re-apply fix. Regression
  test: `core/test/test_y4m_411_oob.c` (ASan-required to catch
  regression deterministically).

- [ADR-0461](../../../docs/adr/0461-cli-validate-dimensions-chroma.md) /
  [ADR-1398](../../../docs/adr/1398-cli-accept-odd-dimensions-chroma-subsampled.md)
  — CLI rejects non-positive input dimensions and accepts odd-sized
  chroma-subsampled frames using ceiling division.
  **Validation invariant**: `validate_video_info()` and
  `validate_chroma_alignment()` = canonical per-stream and
  chroma-alignment gates; if upstream Netflix adds similar checks to
  `validate_videos()` in sync, merge rather than duplicate — keep
  fork's helpers, call them from merged body.
- [ADR-0977](../../../docs/adr/0977-core-tools-input-reader-safety.md) —
  input-reader safety in vendored Daala YUV / Y4M parsers
  (`y4m_input.c`, `yuv_input.c`) and bench binary
  (`vmaf_bench.c`).
  **malloc-return invariant**: `y4m_input_open_impl` must check
  return of every `malloc()`, return -1 on NULL, freeing any
  partial allocation. Pre-fix code returned 0 on OOM and
  caller surfaced NULL `dst_buf` to next `fread`, crashing.
  Upstream Netflix/vmaf still carries unchecked variant; on
  `/sync-upstream` keep fork's NULL check + cleanup block.
  **size_t-precision invariant**: both readers compute `dst_buf_sz`
  with `(size_t)` cast applied to `pic_w` / `pic_h` (Y4M) and
  `width` / `height` (YUV) **before** multiply. 4:4:4 paths
  in `y4m_input.c` already cast for same reason. If upstream
  re-introduces `pic_w * pic_h` in `int` precision on sync, keep
  fork's cast. In `yuv_input.c` that cast now lives in
  `yuv_input_set_plane_geometry()`, which `yuv_input_open` calls in place
  of upstream's `switch` plus `goto fail` label. Helper returns -1 for
  unsupported `pix_fmt`; caller frees reader state and returns NULL, same
  as label did. Sync conflict here resolves to fork's helper, not
  upstream's label — cast must not follow label back.
  **bench GPU-state lifetime invariant**:
  `BenchGpuState` owns CUDA / SYCL handles. `bench_feature()` and
  `run_feature_collect()` execute guarded stages, then call
  `bench_cleanup_resources()` exactly once; `run_sycl_gpu_profile()` does
  same through `cleanup_sycl_profile()`. Keep context close before GPU-state
  free and do not add early returns after ownership begins.
