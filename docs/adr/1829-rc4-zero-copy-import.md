<!-- markdownlint-disable MD013 MD060 -->
# ADR-1829: RC4 owns the whole device-memory import API, not only the first full Rust metric

- **Status**: Accepted
- **Date**: 2026-10-05
- **Deciders**: maintainer
- **Tags**: release, rc, api, abi, gpu, cuda, sycl, hip, metal, roadmap

## Context

[ADR-1490](1490-rc3-rc9-candidate-map-cpu-capability.md) fixed the candidate
map: RC4 is the first full Rust metric (the `vmaf_v1.0.16_3d0h` path), RC5 is
deduplication and `libgpudispatch`. [ADR-1685](1685-post-1-0-embedding-zero-copy-milestone.md)
then put the zero-copy device-frame import API with fences into a post-1.0
embedding milestone (epic [#2067](https://github.com/VMAFx/vmafx/issues/2067)),
built on the pool RC5 provides.

That left the first release with copies on the supported GPU paths:

- CUDA copies a decoder frame device-to-device into libvmaf's own pool.
- SYCL imports luma only; the default model needs chroma and the path refuses
  it ([#2075](https://github.com/VMAFx/vmafx/issues/2075)).
- Metal's IOSurface import is a lock plus a CPU copy
  (`T-GAP-METAL-IOSURFACE-NOT-TRUE-ZERO-COPY`).
- HIP has no import path.
- NV12 and P010 decoder frames are not converted on the GPU everywhere, and the
  FFmpeg filters do not take hardware frames through one API.

The maintainer decided on 2026-10-05 that `v1.0.0` must be zero-copy where a
frame already lives in device memory, and that this is part of RC4 rather than
a further candidate number.

## Decision

We will move the import API from the post-1.0 milestone into RC4. RC4 keeps its
Rust metric and gains, as a second part with its own exit evidence
([#1723](https://github.com/VMAFx/vmafx/issues/1723)):

1. A device-memory import API on `VmafPicture2`
   ([ADR-0928](0928-vmaf-picture-v2-explicit-backend-state.md)), additive
   (HISS-14): CUDA device pointer or array with an event, SYCL USM or DMA-BUF
   with a Level Zero event, HIP device pointer with an event, Metal IOSurface or
   MTLTexture with a shared event, DMA-BUF with a sync file on Linux.
2. Fences in both directions, no release callback without a fence
   ([ADR-1199](1199-cuda-picture-handover-barrier.md)).
3. NV12 and P010 converted on the GPU; CUDA without its device-to-device copy;
   SYCL chroma import and D3D11 on the GPU; Metal IOSurface or MTLTexture bound
   without the CPU copy ([ADR-0423](0423-metal-iosurface-import-scaffold.md));
   a HIP import path; the FFmpeg filters taking hardware frames.

RC4 implements the API per backend. RC5 folds the per-backend parts into
`libgpudispatch`; the API does not change in RC5. The rest of the embedding
milestone (asynchronous window scores, Windows and macOS shared libraries, the
CMake package, the embedding licence documentation, HDR) stays post-1.0, so
this ADR refines ADR-1490 (RC4's scope) and ADR-1685 (where the import API
sits) and supersedes neither. The RC3 to RC9 numbering is unchanged.

Exit evidence for the import part: an imported frame scores bit-identically to
the same frame uploaded from the host on every backend that declares the twin
exact; no host copy of pixel data on the import path, shown with the vendor's
profiler; fence-ordering tests that fail when a fence is skipped.

## Alternatives considered

| Option | Pros | Cons | Outcome |
|---|---|---|---|
| Whole import API in RC4 (**chosen**) | `v1.0.0` is zero-copy on every backend; one API shape settled before RC5 deduplicates; no extra candidate number | RC4 grows beyond the Rust metric; RC4 needs Apple, Intel and AMD hardware for the exit evidence | Chosen |
| Post-1.0, as ADR-1685 placed it | RC4 stays small; no hardware dependency before 1.0 | The first release keeps device-to-device and CPU copies on supported paths; the API lands after RC5 has already fixed the pool | Not chosen |
| Only remove today's copies | Smaller; fixes the Metal and CUDA copies | No fence contract and no HIP path; callers still cannot hand over a frame safely (ADR-1199); the API is designed later against code written without it | Not chosen |
| A new candidate number for it | Keeps RC4 pure | The maintainer ruled it out; shifts RC5 to RC9 again (ADR-1490 did this once) | Not chosen |

## Consequences

- **Positive**: the first release has no avoidable copy on a device-resident
  frame; the import API exists before RC5 extracts `libgpudispatch`, so RC5
  folds finished per-backend code.
- **Negative**: RC4 is larger and depends on device access for every backend;
  the import API and its fences become an ABI commitment at `v1.0.0`.
- **Neutral / follow-ups**: epic #2067 loses its work items 2 to 4 (the import
  API, per-backend fixes and the FFmpeg hardware-frame filters); items for
  asynchronous scores, shared-library builds and HDR stay. No throughput figure
  is claimed before RC8. `docs/state.md` moves the zero-copy rows into the RC4
  disposition. Each backend's import PR carries its own docs
  (`docs/backends/`, `docs/api/`).

## References

- `Q`: "Whole import API in RC4" (popup answer, 2026-10-05)
- `req` (verbatim): "well and if we are not fully zero copy then this is a new part of rc4 lol... not another number"
- [ADR-1490](1490-rc3-rc9-candidate-map-cpu-capability.md), [ADR-1685](1685-post-1-0-embedding-zero-copy-milestone.md), [ADR-1421](1421-rc3-rc8-candidate-map.md)
- [ADR-0928](0928-vmaf-picture-v2-explicit-backend-state.md), [ADR-1199](1199-cuda-picture-handover-barrier.md), [ADR-0423](0423-metal-iosurface-import-scaffold.md)
- Issues [#1723](https://github.com/VMAFx/vmafx/issues/1723), [#2067](https://github.com/VMAFx/vmafx/issues/2067), [#2075](https://github.com/VMAFx/vmafx/issues/2075)
