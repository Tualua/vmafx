<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1486: `float_motion` scales its scale-1 planes with the stride it was called with; upstream recomputes the stride from the plane width, and `motion_add_scale1` with `motion_add_uv` differs by up to 25

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `upstream-divergence`, `motion`, `correctness`, `rc3`, `fork-local`

## Context

The reference for code the fork inherited from Netflix/vmaf is Netflix's
source; a difference needs an ADR that names it and gives its size (see
References). [ADR-1033](1033-cpu-scoring-nan-ub-guards.md) (item 9) made this
change as one of eleven guards and says neither what upstream does nor how
far the fork is from it. ADR-1033 is accepted and is not edited; this ADR adds
the comparison.

Upstream, `vmaf_image_sad_c()` in `libvmaf/src/feature/motion.c` at Netflix
`9e48141b`: with `motion_add_scale1` it builds a half-resolution copy of both
inputs and passes `ALIGN_CEIL(width * sizeof(float)) / sizeof(float)` as the
stride of the source (`:70`, `:75`, `:76`), not the `img1_stride` and
`img2_stride` it was called with. For luma the two are equal. With
`motion_add_uv` the chroma planes come from buffers `float_motion` allocated
for the luma width, so the stride computed from the chroma width is not the
buffer's stride and every chroma row after the first is read from the wrong
offset.

The fork, `core/src/feature/motion.c`, since PR #641 (`a295f4a70`,
2026-06-04): the two scaling calls take `img1_stride` and `img2_stride`.

## Decision

The fork keeps the caller's strides. `float_motion` with `motion_add_scale1`
and `motion_add_uv` together differs from upstream by design; every other
option combination is not affected.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Restore upstream's recomputed stride | Same code as upstream | The chroma contribution is computed from samples of other rows; the function's own contract says its strides describe the buffers passed in | The unscaled sum of the same function honours the strides |
| Refuse the option combination | No value that differs from upstream | Removes a combination upstream accepts; the correct stride is known | The fix is two arguments |

## Consequences

- **Measured size** (audit of 2026-10-02: Netflix `cea2b4d8`, whose
  `motion.c` is `9e48141b`'s, against the fork with its unintended
  differences reverted; GCC 16.2.1, x86-64, C API at `%.17g`, scalar and
  default dispatch;
  `motion_add_scale1=true:motion_add_uv=true:motion_filter_size=3`): the
  Netflix 576x324 pair, 47 of 48 `motion` values differ, by up to 0.47
  (frame 24: upstream 17.854148626327515, fork 18.324933052062988); the 10-bit
  pair 0.44; the 160x90 pair 0.058; the noise pair 25.1 (frame 2: upstream
  194.26560592651367, fork 219.34847259521484). `motion2` and `motion3`
  follow. With either option alone, and at the default, `float_motion` is
  identical to upstream.
- **Upstream status**: Netflix/vmaf#1667 (open, sent by the fork) makes the
  same change.
- **Ends when** upstream merges #1667. The allowlist entry of the upstream
  parity guard then goes stale and is removed.

## References

- Fork PR #641 (`a295f4a70`); [ADR-1033](1033-cpu-scoring-nan-ub-guards.md)
  item 9.
- Upstream: `libvmaf/src/feature/motion.c:70`, `:75`, `:76` at Netflix
  `9e48141b`; Netflix/vmaf#1667.
- Source: `req` (popup answer, 2026-10-02): "Netflix's source, deviations
  only by ADR".
