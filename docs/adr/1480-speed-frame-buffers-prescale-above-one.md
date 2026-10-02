<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1480: `speed_temporal` sizes its frame buffers for the prescaled height; upstream sizes them for the source height and reads past them when `speed_prescale` is above 1

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `upstream-divergence`, `speed`, `memory-safety`, `correctness`, `rc3`, `fork-local`

## Context

The reference for code the fork inherited from Netflix/vmaf is Netflix's
source; a difference needs an ADR (see References). The upstream parity audit
of 2026-10-02 found this difference without one.

Upstream, `libvmaf/src/feature/speed.c` at Netflix `9e48141b`: `speed_temporal`
allocates its four frame buffers as `float_stride * h`, the source height
(`:1578` to `:1582`). `filter_and_downscale()` copies
`stride_px * dim.alloc_height` floats out of such a buffer (`:957`, `:968`),
and `alloc_height` is the larger of the source and the prescaled height
(`:1049`). With `speed_prescale` above 1 the prescaled frame is taller than
the source, so the copy reads past the buffer and the prescaled frame is
written past it. `speed_chroma` in the same file allocates with
`alloc_height` (`:1352`, `:1356`) and is not affected.

The fork's `core/src/feature/speed.c` allocates the four buffers as
`float_stride * alloc_height` (PR #1643, `c1a2b1914`, 2026-09-30). The pull
request called it a bug fix matching the fork's own upstream pull request
and wrote no ADR.

## Decision

The fork keeps `speed_temporal`'s frame buffers at `alloc_height` rows. With
`speed_prescale` above 1 its scores differ from upstream's by design: upstream
computes them from memory outside its buffers. At `speed_prescale` of 1 or
less `alloc_height` equals the source height and nothing differs.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Restore upstream's allocation | Same code as upstream | Heap overrun on every frame at `speed_prescale` above 1; the value depends on what lies behind the buffer, and upstream's own scalar and SIMD runs disagree there | A memory-safety defect is not a reference |
| Refuse `speed_prescale` above 1 | No value that differs from upstream | Removes an option value upstream documents; the correct allocation is known | The fix is one multiplication |

## Consequences

- **Measured size** (audit of 2026-10-02: Netflix `cea2b4d8`, whose `speed.c`
  is `9e48141b`'s, against the fork with its unintended differences reverted;
  GCC 16.2.1, x86-64, C API at `%.17g`;
  `speed_prescale=2.0:speed_prescale_method=lanczos4`): scalar dispatch, the
  Netflix 576x324 pair: 1 of 48 frames identical, at most 195 apart (frame 36:
  upstream 208.31849670410156, fork 13.179099082946777); 1920x1080
  checkerboard 131; the 10-bit pair 117; the noise pair 12.3. Upstream ends in
  a segmentation fault on the 160x90 pair (both dispatches) and on the noise
  pair (default dispatch); the fork returns finite values on all five. Between
  its own scalar and default dispatch, upstream's `speed_temporal` agrees on
  1096 of 1122 values of the probe set (at most 131 apart); the fork's
  dispatches agree on all.
- **Upstream status**: Netflix/vmaf#1627 (open, sent by the fork) makes the
  same allocation change; Netflix/vmaf#1626 is the report.
- **Ends when** upstream merges #1627. The values then agree, and the
  allowlist entry of the upstream parity guard for this deviation goes stale
  and is removed.
- **Neutral**: PR #1643 also made `speed_prescale_resamples()` resample when
  rounding changes a plane extent although the factor is within 0.001 of 1;
  no option value of the audit reaches that case. Its `speed_chroma` part
  (chroma extents rounded up on odd sizes) belongs to
  [ADR-1483](1483-odd-size-chroma-planes-round-up.md).
- **Neutral**: `core/test/test_speed_frame_buffers.c` guards the allocation.

## References

- Fork PR #1643 (`c1a2b1914`); `docs/state.md` row
  `T-SPEED-TEMPORAL-PRESCALE-UP-OVERFLOW-2026-09-30`.
- Upstream: `libvmaf/src/feature/speed.c:957`, `:968`, `:1049`, `:1578` at
  Netflix `9e48141b`; Netflix/vmaf#1627, Netflix/vmaf#1626.
- Source: `req` (popup answer, 2026-10-02): "Netflix's source, deviations
  only by ADR".
