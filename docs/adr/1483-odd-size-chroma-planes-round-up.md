<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1483: a subsampled chroma plane of an odd-sized picture is rounded up; upstream rounds down, and chroma metrics on odd sizes differ by up to 0.83 dB

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `upstream-divergence`, `core`, `picture`, `chroma`, `odd-dimensions`, `correctness`, `rc3`, `fork-local`

## Context

The reference for code the fork inherited from Netflix/vmaf is Netflix's
source; a difference needs an ADR (see References). The upstream parity audit
of 2026-10-02 found this difference without one: the commit that made it
called it a bug fix.

Upstream, `libvmaf/src/picture.c:74` and `:76` at Netflix `9e48141b`:
`pic->w[1] = pic->w[2] = w >> ss_hor;` and
`pic->h[1] = pic->h[2] = h >> ss_ver;`. A 4:2:0 picture of 19x19 has chroma
planes of 9x9. A raw 4:2:0 or 4:2:2 file stores `ceil(w / 2)` chroma samples
per row (and `ceil(h / 2)` rows for 4:2:0), so the last chroma column and row
of an odd-sized frame are not part of the picture, and the rightmost luma
column and the bottom luma row have no chroma sample of their own.

The fork rounds up: `vmaf_chroma_extent()` (`core/src/picture_geometry.h`),
called by `picture_compute_geometry()` in `core/src/picture.c` and by every
extractor and twin that sizes a chroma buffer itself. Commit `4f08d32b2`
(2026-05-10) introduced it after a 577x323 4:2:0 input made `ciede` read one
row past the chroma allocation (an ASan heap out-of-bounds read in
`scale_chroma_planes()`). PR #1643 moved the definition into one helper and
[ADR-1398](1398-cli-accept-odd-dimensions-chroma-subsampled.md) made the CLI
accept odd raw inputs with that geometry.

## Decision

The fork keeps `ceil` for subsampled chroma extents: every luma sample of an
odd-sized picture is covered by a chroma sample, and the picture holds the
planes as a file stores them. Metrics that read chroma differ from upstream
on odd sizes by design; luma metrics and even sizes are not affected.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Restore upstream's `floor` | Chroma metrics equal upstream on odd sizes | The last chroma row and column of the file are dropped; an extractor that upsamples chroma to the luma size has no sample for the last luma row and column, which is where `ciede` read out of bounds | The out-of-bounds read is what the change fixed |
| Keep `floor` and fix each consumer to clamp | Same plane sizes as upstream | Every consumer and every GPU twin needs its own clamp; the dropped samples stay dropped | One definition in one helper is checked once |

## Consequences

- **Measured size** (audit of 2026-10-02: Netflix `cea2b4d8`, whose
  `picture.c` is `9e48141b`'s, against the fork with its unintended
  differences reverted; GCC 16.2.1, x86-64, C API at `%.17g`; the 4:2:0 pairs
  of 19x19, 17x17 and 12x9, 7 frames; the harness reads the file with rounded-up
  chroma rows and gives each tree what its picture holds): `psnr_cb` differs
  on 7 of 7 frames by up to 0.684 dB and `psnr_cr` by up to 0.826 dB (19x19:
  `psnr_cr` upstream 33.759956236901679, fork 32.933835937080573);
  `ciede2000` differs on the same frames by up to 0.198. `psnr_y` and every
  other luma metric are identical on those frames, and the 573x163 4:4:4 pair
  shows no difference.
- **Upstream status**: Netflix/vmaf#1604 (open, sent by the fork) makes
  upstream's tools read odd-sized files; it keeps `floor` in the picture and
  drops the trailing chroma column and row in the reader. No upstream pull
  request changes `picture.c`.
- **Ends when** upstream sizes subsampled chroma planes by rounding up. Until
  then the allowlist of the upstream parity guard lists the chroma metrics on
  odd-sized subsampled fixtures.
- **Neutral**: `core/test/test_picture.c` asserts the plane sizes for odd
  4:2:0, 4:2:2 and 4:4:4 pictures. No Netflix golden fixture has an odd size.

## References

- Fork commit `4f08d32b2` (subject: "ceiling division for odd-dim YUV 4:2:0
  chroma planes"); fork PR #1643 (`c1a2b1914`);
  [ADR-1398](1398-cli-accept-odd-dimensions-chroma-subsampled.md).
- Upstream: `libvmaf/src/picture.c:74`, `:76` at Netflix `9e48141b`;
  Netflix/vmaf#1604.
- Source: `req` (popup answer, 2026-10-02): "Netflix's source, deviations
  only by ADR".
