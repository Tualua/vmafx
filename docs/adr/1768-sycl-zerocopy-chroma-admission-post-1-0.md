<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1768: Post-1.0, the SYCL zero-copy path imports chroma and admits every SYCL twin

- **Status**: Proposed (post-1.0, [ADR-1685](1685-post-1-0-embedding-zero-copy-milestone.md) milestone 8)
- **Date**: 2026-10-05
- **Deciders**: lusoris (proposed); maintainer acceptance pending
- **Tags**: `sycl`, `zero-copy`, `chroma`, `correctness`, `roadmap`, `fork-local`

## Context

[ADR-1688](1688-sycl-zero-copy-luma-only-admission.md) made the SYCL
zero-copy path luma-only by decision: `vmaf_read_pictures_sycl()` admits a
registered extractor only when its `reads_shared_luma_only()` hook answers true,
and refuses everything else (the library's default model `vmaf_v1.0.16_3d0h`
among it) by name with `-ENOTSUP`. It considered importing the chroma and moving
every chroma twin onto device planes, and deferred that to the post-1.0
embedding milestone of
[ADR-1685](1685-post-1-0-embedding-zero-copy-milestone.md), saying it needs its
own ADR.

The Tualua fork had implemented exactly that before ADR-1688 landed, measured on
an Arc A380 (its ADR-1597 to ADR-1599, here
[ADR-1765](1765-sycl-zerocopy-planar-chroma-import.md),
[ADR-1766](1766-sycl-host-staging-to-shared-planes.md) and
[ADR-1767](1767-sycl-float-motion-add-uv.md)): one layout-addressed kernel
de-interleaves the VA surface's UV plane into the shared planar Cb / Cr planes,
every host-staging twin reads the shared planes instead of host pictures, and
`float_motion_sycl` implements `motion_add_uv`. With it, the FFmpeg e2e harness
finds every SYCL twin on zero-copy input equal to host upload and host upload
equal to the CPU, at 8-bit NV12 and 10-bit P010
(`pass=50 fail=0 nonexact=0`, Research-1765).

This record is the "own ADR" ADR-1688 asks for, and places that work in the
post-1.0 milestone.

## Decision

Proposed for milestone 8 (post-1.0), not for RC1 to RC9:

- The zero-copy import fills the shared chroma planes as well as luma
  (ADR-1765), and every SYCL twin reads only the shared planes (ADR-1766,
  ADR-1767).
- ADR-1688's admission check stays, with a wider meaning: its hook answers
  "this twin computes from the shared device planes the zero-copy import fills"
  (luma and chroma). Every SYCL twin answers true for any options. A CPU
  extractor, or a SYCL extractor without the hook, is still refused by name
  before the frame is counted.
- A chroma reader whose input carried no chroma (a caller that writes the luma
  buffers only, or the Windows D3D11 import) is refused at its `submit()` with
  `-ENOTSUP` and a message naming it (`vmaf_sycl_require_chroma()`), never fed
  stale planes.
- The hook keeps its name, `reads_shared_luma_only`, in this change, so that
  ADR-1688's code, tests and records stay recognisable; renaming it (for
  example to `reads_shared_planes`) is a follow-up for the maintainer to take
  or leave.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
| --- | --- | --- | --- |
| Land chroma import and the shared-plane twins post-1.0, widening ADR-1688's hook (proposed) | The default model and every feature run zero-copy with the CPU's scores; ADR-1688's single admission check and its tests carry over | A larger change to SYCL host code; extra device and pinned memory for chroma planes (4.1 MB / 2.1 MB at 1080p 8-bit) | Proposed |
| Keep ADR-1688's luma-only path through 1.x | No change | The default model needs the host-frame bridge on QSV input | The work exists and is measured; ADR-1685 already plans the import |
| Land it before 1.0 | Users get it sooner | Contradicts ADR-1685's milestone split (no work added to RC1 to RC9) | Maintainer decision on 2026-10-05: post-1.0 |
| Rename the hook in the same change | Accurate name | Touches every ADR-1688 file again; more conflicts with in-flight SYCL work | Left as a follow-up |

## Consequences

- **Positive**: `vmaf_v1.0.16_3d0h` and every SYCL twin score QSV / VA input
  zero-copy, equal to host upload.
- **Negative**: the chroma planes are allocated for every zero-copy context;
  1080p 10-bit throughput moved -3.9 % (8-bit +1.9 %) against the luma-only
  baseline on the A380, inside a 3 % baseline spread; 4K not measured. The
  hook's name no longer describes it until it is renamed.
- **Neutral / follow-ups**: D3D11 chroma import (Windows) is out of scope; the
  hook rename; re-measuring on Xe2 hardware.

## References

- `req` (user, 2026-10-05): "PR 2b (post-1.0, prepare only): chroma import + shared planes + motion_add_uv ... framed as superseding/extending ADR-1688 for the post-1.0 milestone named in ADR-1685 (new ADR states that)."
- [ADR-1688](1688-sycl-zero-copy-luma-only-admission.md), [ADR-1685](1685-post-1-0-embedding-zero-copy-milestone.md), [ADR-1765](1765-sycl-zerocopy-planar-chroma-import.md), [ADR-1766](1766-sycl-host-staging-to-shared-planes.md), [ADR-1767](1767-sycl-float-motion-add-uv.md), [ADR-1764](1764-sycl-filter-twin-routing.md).
- Research digest: [Research-1765](../research/1765-sycl-zerocopy-feature-correctness.md).
