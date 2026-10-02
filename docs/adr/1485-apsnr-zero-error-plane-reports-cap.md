<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1485: the aggregate PSNR of a plane without error is the per-frame cap; upstream publishes a ceiling 54 dB above it on the 1080p checkerboard chroma

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `upstream-divergence`, `psnr`, `correctness`, `numerics`, `rc3`, `fork-local`

## Context

The reference for code the fork inherited from Netflix/vmaf is Netflix's
source; a difference needs an ADR that names it and gives its size (see
References). [ADR-1033](1033-cpu-scoring-nan-ub-guards.md) (item 1) made this
change as one of eleven guards and says neither what upstream does nor how
far the fork is from it. ADR-1033 is accepted and is not edited; this ADR adds
the comparison.

Upstream, `flush()` in `libvmaf/src/feature/integer_psnr.c:226` to `:251` at
Netflix `9e48141b`, with `enable_apsnr`: for each of three planes

```c
apsnr     = 10 * (log10(peak * peak) + log10(n_pixels) - log10(sse));
max_apsnr = ceil(10 * log10(peak * peak * n_pixels * 2));
/* publishes MIN(apsnr, max_apsnr) */
```

For a plane whose frames are all identical `sse` is 0, `apsnr` is `+inf` and
the published value is `max_apsnr`. The per-frame PSNR of the same plane is
capped at `psnr_max` (`6 * bpc + 12` by default: 60 dB at 8 bits). With
`enable_chroma=false` the loop still runs over three planes; the two chroma
planes then have `sse = 0` and `n_pixels = 0`.

The fork, `flush()` in `core/src/feature/integer_psnr.c` with
`vmaf_psnr_aggregate()` (`core/src/feature/psnr_score.h`), since PR #641
(`a295f4a70`, 2026-06-04): a plane with `sse == 0` reports `psnr_max` of that
plane; the ceiling is `ceil(10 * log10(peak * peak * n_pixels))`, without the
factor 2; and the loop runs over one plane when chroma is disabled.

## Decision

The fork keeps all three parts. The aggregate of a plane without error is
that plane's per-frame cap, and `apsnr_cb` / `apsnr_cr` are not published
when chroma is disabled.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Restore upstream's `flush()` | Same code as upstream | The aggregate of a plane that sits at the cap on every frame is far above the cap, and grows with the length of the clip; `enable_chroma=false` publishes two aggregates of planes that were never measured | An aggregate above every per-frame value it aggregates is not a reference |
| Keep the factor 2 in the ceiling, as the fork's upstream pull request does | One line closer to upstream | None in value: with `sse >= 1` the aggregate is at most `10 * log10(peak^2 * n_pixels)`, which is below either ceiling, so the ceiling is reached only at `sse == 0`, and that case no longer uses it | Equal in value. At the sync that brings upstream's fix the fork takes upstream's lines |

## Consequences

- **Measured size** (audit of 2026-10-02: Netflix `cea2b4d8`, whose
  `integer_psnr.c` is `9e48141b`'s, against the fork with its unintended
  differences reverted; GCC 16.2.1, x86-64, C API at `%.17g`, scalar and
  default dispatch): `psnr=enable_apsnr=true` on the 1920x1080 checkerboard
  pair with 1 pixel squares, whose chroma planes are identical: `apsnr_cb`
  and `apsnr_cr` are 114 upstream and 60 in the fork. With `min_sse=0.5` and
  `reduced_hbd_peak` they are 114 and 109 (the cap `min_sse` derives). Every
  other aggregate and every per-frame value of the `psnr` option variants is
  identical. The audit had no variant with `enable_apsnr` and
  `enable_chroma=false` together; upstream's pull request reports `-inf`
  there.
- **Upstream status**: Netflix/vmaf#1666 (open, sent by the fork) publishes
  `psnr_max` for a plane without error and aggregates only the measured
  planes; it keeps the factor 2.
- **Ends when** upstream merges #1666. The allowlist entry of the upstream
  parity guard then goes stale and is removed.

## References

- Fork PR #641 (`a295f4a70`); [ADR-1033](1033-cpu-scoring-nan-ub-guards.md)
  item 1.
- Upstream: `libvmaf/src/feature/integer_psnr.c:226` to `:251` at Netflix
  `9e48141b`; Netflix/vmaf#1666.
- Source: `req` (popup answer, 2026-10-02): "Netflix's source, deviations
  only by ADR".
