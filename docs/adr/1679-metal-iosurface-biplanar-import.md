<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1679: The Metal IOSurface import reads NV12 and P010 surfaces itself, and the FFmpeg filter imports whole frames

- **Status**: Accepted
- **Date**: 2026-10-05
- **Deciders**: Lusoris
- **Tags**: metal, ffmpeg, api, correctness, fork-local

## Context

The FFmpeg `libvmaf_metal` filter (patch `0013`) scores VideoToolbox frames
without a download. VideoToolbox decodes 8-bit 4:2:0 to NV12 and 10-bit 4:2:0
to P010. Both are bi-planar: one luma plane and one plane of interleaved Cb/Cr
samples. P010 keeps its 10 bits in the most significant bits of each 16-bit
sample. libvmaf scores planar 4:2:0 with the samples in the least
significant bits.

The filter imported plane 0 only. `vmaf_metal_state_build_pictures()` in
`core/src/metal/picture_import.mm` requires all three planes, so
`vmaf_metal_read_imported_pictures()` returned `-EINVAL` on the first frame
and the filter failed with `AVERROR_EXTERNAL`. Importing planes 1 and 2 would
not have helped. The public header told the caller to de-interleave first,
but `vmaf_metal_picture_import()` takes an `IOSurfaceRef`, and a caller holds
no planar surface to pass. The import also copied the surface's plane `n` as
picture plane `n` without reading the surface's pixel format:

- picture plane 1 would have been the interleaved CbCr bytes;
- plane 2 does not exist on a bi-planar surface;
- a P010 sample would have been scored 64 times too large.

ADR-1121 fixed the same P010 defect in the SYCL path.

The import is a CPU copy today (ADR-0423; true GPU binding is the open
`GAP-METAL-IOSURFACE-NOT-TRUE-ZERO-COPY` row). So a de-interleave and a shift
done during that copy cost no extra pass over the frame.

## Decision

`vmaf_metal_picture_import()` reads the surface's CoreVideo pixel format
(`IOSurfaceGetPixelFormat`) and plans each plane's read from a table in
`core/src/metal/iosurface_layout.h`. The table holds NV12 (`'420v'`,
`'420f'`, `bpc` 8), P010 (`'x420'`, `'xf20'`, `bpc` 10, shift 6) and planar
8-bit 4:2:0 (`'y420'`, `'f420'`):

- picture plane 0 is the luma plane;
- on a bi-planar surface, planes 1 and 2 are the even and odd samples of the
  second plane;
- P010 samples are shifted down while they are copied.

A pixel format outside the table returns `-ENOTSUP`. A surface whose plane
count, element size, bit depth or plane size does not match the frame returns
`-EINVAL`. In both cases nothing is copied, so the wrong layout is never
scored.

The filter checks both inputs. It accepts NV12 and P010 software formats only,
and names a refused format in its error. It imports planes 0, 1 and 2 of both
frames. A frame that cannot be imported fails the filter instead of passing
through unscored. The header is plain C, so
`core/test/test_metal_iosurface_layout.c` runs the copy on every host.
`test_metal_iosurface_import_parity` runs the import on real IOSurfaces in the
macOS tester bundle.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| De-interleave in libvmaf's import, keyed by the surface's pixel format (chosen) | No new symbol. The layout comes from the surface, not from a caller who could get it wrong. Callers that pass planar 8-bit surfaces keep working. The copy is the one the import already makes. | Changes what `plane` 1 and 2 mean for a bi-planar surface. The documented contract ("caller de-interleaves") changes. | Chosen. No caller could meet the old contract with an `IOSurfaceRef`. |
| New entry point `vmaf_metal_picture_import_frame()` that imports all planes in one call | FFmpeg's configure could probe for it, so an old libvmaf would fail at configure time. | A second import entry point for one behaviour (HISS-19). The single-plane call would stay with a contract nobody can meet. | Rejected: one behaviour, one implementation. The filter and libvmaf ship from one tree. |
| De-interleave in the FFmpeg filter: lock the `CVPixelBuffer`, build planar `VmafPicture`s, call `vmaf_read_pictures()` | No libvmaf change. | Bypasses the import API the filter exists to use. Every other caller of the import keeps the defect. | Rejected: the defect is in libvmaf's contract, not in one caller. |
| Refuse bi-planar surfaces with an error and require `hwdownload` | Smallest change. | VideoToolbox decodes to nothing else at 4:2:0, so the filter would score nothing. | Rejected. |
| De-interleave on the GPU with a Metal kernel | Ready for a true zero-copy path. | The import is a CPU copy today (ADR-0423). A kernel needs the texture binding the open zero-copy row is about. | Deferred to that row and to the post-1.0 zero-copy import ([ADR-1685](1685-post-1-0-embedding-zero-copy-milestone.md)). The table and the plan stay valid for it. |

## Consequences

- **Positive**: the filter can score NV12 and P010 VideoToolbox frames. A
  layout libvmaf does not know is refused with `-ENOTSUP` instead of scored
  as something else. The copy is tested on every host.
- **Negative**: 4:2:2 and 4:4:4 VideoToolbox formats (`nv16`, `p210`, `nv24`,
  `p410`, `p416`) are refused. They need `hwdownload` and the `libvmaf`
  filter's `metal_device` option.
- **Neutral / follow-ups**: nothing here ran on an Apple device. The row
  `T-METAL-FFMPEG-FILTER-BIPLANAR-IMPORT-2026-10-05` in `docs/state.md` stays
  open. It closes on two checks:
  - a macOS tester report that shows `test_metal_iosurface_import_parity`
    passing;
  - an FFmpeg run of `libvmaf_metal` that gives the scores of the `libvmaf`
    filter on the same decoded frames.

## References

- CoreVideo `CVPixelBuffer.h` (macOS 11.3 SDK, `MacOSX-SDKs` mirror,
  read 2026-10-05):
  - `kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange = '420v'` and
    `kCVPixelFormatType_420YpCbCr8BiPlanarFullRange = '420f'`;
  - `kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange = 'x420'` and
    `kCVPixelFormatType_420YpCbCr10BiPlanarFullRange = 'xf20'`, "2 plane
    YCbCr10 4:2:0, each 10 bits in the MSBs of 16bits";
  - `kCVPixelFormatType_420YpCbCr8Planar = 'y420'` and
    `kCVPixelFormatType_420YpCbCr8PlanarFullRange = 'f420'`.
- FFmpeg n9.0.2 `libavutil/hwcontext_videotoolbox.c`: the table maps NV12 and
  P010 to those types. `libavcodec/videotoolbox.c:819` requests
  IOSurface-backed buffers. `libavcodec/videotoolboxenc.c:54-55` defines
  `'xf20'` and `'x420'`.
- [ADR-0423](0423-metal-iosurface-import-scaffold.md) (the import),
  [ADR-1121](1121-sycl-qsv-zerocopy-p010-normalization.md) (the SYCL P010
  shift), [ADR-1496](1496-metal-gate-in-tester-bundle.md) (the macOS tester
  bundle's Metal rows).
- Source: maintainer task of 2026-10-05, paraphrased: fix the
  `libvmaf_metal` patch so an NV12 or P010 hardware frame reaches libvmaf
  correctly, or fails with an error naming the unsupported format, and never
  produces a silent wrong score.
