<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1479: `ciede` upsamples 4:2:2 chroma with the horizontal flag for columns and the vertical flag for rows; upstream has the two swapped, and `ciede2000` differs by up to 0.153 on 4:2:2 input

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `upstream-divergence`, `ciede`, `correctness`, `chroma`, `rc3`, `fork-local`

## Context

The reference for code the fork inherited from Netflix/vmaf is Netflix's
source; a difference needs an ADR (see References). The upstream parity audit
of 2026-10-02 found this difference without one.

`ciede` upsamples the chroma planes to the luma size before it converts to
Lab. Upstream, `libvmaf/src/feature/ciede.c` at Netflix `9e48141b`:

```c
out_buf[j] = in_buf[(j / ((p && ss_ver) ? 2 : 1))];        /* :71 (16 bit), :89 (8 bit) */
in_buf += ((p && ss_hor) ? i % 2 : 1) * in->stride[p];     /* :73 (16 bit, / 2), :91 */
```

The column index is halved when the format is subsampled vertically, and the
input row advances every second output row when it is subsampled
horizontally. For 4:2:0 both flags are 1 and for 4:4:4 both are 0, so the swap
shows only on 4:2:2 (`ss_hor = 1`, `ss_ver = 0`). There the column index runs
to `w - 1` in a chroma row that holds `w / 2` samples (the right half of every
output row is read from beyond the row, and on the last row from beyond the
plane), and only the top half of the chroma rows is used.

The fork fixed it in PR #1050 (`8af3cf3e0`, 2026-06-27):
`core/src/feature/ciede.c` uses `ss_hor` for the column index and `ss_ver` for
the row advance in `scale_chroma_planes()` and `scale_chroma_planes_hbd()`.
The pull request recorded it as a bug fix with no ADR ("heap OOB read + wrong
scores on YUV422P").

## Decision

The fork keeps its 4:2:2 chroma upsampling: columns by `ss_hor`, rows by
`ss_ver`. `ciede2000` on 4:2:2 input differs from upstream by design until
upstream fixes the swap; 4:2:0 and 4:4:4 are not affected by this deviation.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Restore upstream's expressions | `ciede2000` equals upstream on 4:2:2 | Reads outside the chroma row and, on the last row, outside the plane; scores a picture whose chroma is half missing and half foreign memory | A score computed from memory outside the plane is not a reference to follow |
| Refuse 4:2:2 in `ciede` until upstream fixes it | No value that differs from upstream | Removes a format upstream accepts; the correct value is known | The fix is two flags |

## Consequences

- **Measured size** (audit of 2026-10-02: Netflix `cea2b4d8`, whose `ciede.c`
  is `9e48141b`'s, against the fork with its unintended differences reverted;
  GCC 16.2.1, x86-64, C API at `%.17g`, scalar and default dispatch): on the
  Netflix 576x324 pair as 10-bit 4:2:2, 48 of 48 frames differ, by at most
  0.153 (frame 6: upstream 33.268147359332225, fork 33.11479237659772). The
  4:2:0 and 4:4:4 fixtures show no difference from this change.
- **Upstream status**: Netflix/vmaf#1611 (open, by another contributor) makes
  the same two-flag change. The fork has sent no pull request of its own for
  it.
- **Ends when** upstream merges #1611 or an equivalent fix. At that sync the
  fork takes upstream's lines, and the allowlist entry of the upstream parity
  guard for this deviation goes stale and is removed.
- **Neutral**: `core/test/test_ciede.c` holds the 4:2:2 regression tests PR
  #1050 added. No Netflix golden assertion scores 4:2:2 `ciede`.

## References

- Fork PR #1050 (`8af3cf3e0`); `docs/state.md` row
  `T-BUGHUNT-FEATURE-CPU-2026-06-27`.
- Upstream: `libvmaf/src/feature/ciede.c:71`, `:73`, `:89`, `:91` at Netflix
  `9e48141b`; Netflix/vmaf#1611.
- Source: `req` (popup answer, 2026-10-02): "Netflix's source, deviations
  only by ADR".
