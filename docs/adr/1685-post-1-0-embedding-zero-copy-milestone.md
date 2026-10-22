<!-- markdownlint-disable MD013 MD060 -->
# ADR-1685: A post-1.0 embedding milestone: zero-copy device-frame import with fences, asynchronous window scores, and an unchanged licence

- **Status**: Accepted (Superseded-in-part 2026-10-05 by [ADR-1852](1852-vmafx-api-redesign.md) for the API shape: a new `vmafx` header family and library with `libvmaf.h` as its compatibility layer)
- **Date**: 2026-10-05
- **Deciders**: maintainer
- **Tags**: api, abi, gpu, cuda, sycl, hip, metal, license, release, roadmap, docs

## Context

Encoders and media pipelines want to hand VMAFx frames that already sit in GPU
memory and read quality scores back while the encode is still running. The
targets are FFmpeg and GStreamer pipelines and commercial encoder SDKs. An
earlier draft of this plan (epic [#2067](https://github.com/VMAFx/vmafx/issues/2067))
was checked against the code, the ADRs and external sources on 2026-10-05, and
several of its premises did not hold. The decisions below are the ones where
another engineer could reasonably have chosen differently.

What the tree has today:

- SYCL imports a DMA-BUF / VA surface on the GPU, luma only
  ([ADR-1121](1121-sycl-qsv-zerocopy-p010-normalization.md)). CUDA shares a
  context but copies a decoder frame device-to-device into libvmaf's own pool.
  Metal's IOSurface import is a lock plus a CPU copy
  ([ADR-0423](0423-metal-iosurface-import-scaffold.md); true binding is the
  RC8 row `T-GAP-METAL-IOSURFACE-NOT-TRUE-ZERO-COPY` in
  [`docs/state.md`](../state.md)). HIP has no import path.
- Ownership across a producer and libvmaf is not specified beyond a release
  callback. [ADR-1199](1199-cuda-picture-handover-barrier.md) is the fork's own
  evidence that handing over a picture without an ordering primitive races: a
  caller-written CUDA picture had no recorded event, and one frame per bad run
  came out wrong.
- `vmaf_score_pooled()` pools a window mid-stream and returns `-EAGAIN` until
  the frames are done; there is no non-blocking form. A window is not final
  until the next frame arrives, because `motion2` / `motion3` of a frame need
  its successor.
- The best measured full-model cost is 4.87 ms per frame at 4K 8-bit on an
  RTX 4090 ([ADR-1406](1406-cuda-cli-pinned-host-picture-pool.md)). A GOP costs
  the sum of its frames; a sub-millisecond GOP figure has no basis.
- The library is `libvmaf` (Meson build, pkg-config, C ABI promised stable),
  and the picture type already has a v2 with explicit per-backend state
  ([ADR-0928](0928-vmaf-picture-v2-explicit-backend-state.md)).
  `libgpudispatch` is extracted in RC5 ([#1455](https://github.com/VMAFx/vmafx/issues/1455),
  [ADR-1421](1421-rc3-rc8-candidate-map.md)), and benchmarks come in RC8
  ([ADR-1490](1490-rc3-rc9-candidate-map-cpu-capability.md)).
- Fork-authored files are EUPL-1.2 and Netflix-inherited files stay
  BSD-2-Clause-Patent ([ADR-1250](1250-eupl-fork-relicense.md)); every published
  artifact carries its licence texts
  ([ADR-1503](1503-tester-artifact-licensing.md),
  [ADR-1513](1513-production-artifact-licensing.md)). Embedding in a proprietary
  product therefore raised a licensing question.

The first-release sequence ([ADR-1341](1341-rc-correctness-benchmark-retrain-sequence.md))
keeps RC3 to RC9 for correctness, measurement and training, so the embedding
work needs a place that does not leak into those phases.

## Decision

We will open a new milestone, **Post-1.0 — Embedding & zero-copy encoder
integration** ([milestone 8](https://github.com/VMAFx/vmafx/milestone/8)), whose
work starts after `v1.0.0` and is tracked by epic #2067. It adds no work to RC1
to RC9. Within it:

1. **Licensing.** EUPL-1.2 and BSD-2-Clause-Patent stay as they are. No dual or
   commercial licence. The documentation gains an embedding section stating what
   an embedder must do (keep the Netflix notice, point to the source, share
   changes to libvmaf). Not legal advice.
2. **Commercial encoder SDKs are a target.** Windows `.dll` and macOS `.dylib`
   release builds, a CMake package config exported by the Meson install (no
   CMake build), and the embedding API become release scope after `v1.0.0`.
3. **API shape.** The library stays `libvmaf`; there is no `libvmafx_core` and
   no new header family. New API is additive on `VmafPicture2`
   ([ADR-0928](0928-vmaf-picture-v2-explicit-backend-state.md)), sits on the pool
   RC5's `libgpudispatch` provides, and is one implementation for CUDA, SYCL, HIP
   and Metal (HISS-14, HISS-19).
4. **Fences in both directions, no bare release callback.** The producer's write
   is fenced before VMAFx reads; VMAFx's read is fenced before the producer
   reuses the buffer ([ADR-1199](1199-cuda-picture-handover-barrier.md) is the
   precedent). An imported frame scores bit-identically to the same frame
   uploaded from the host.
5. **Asynchronous window scores.** A non-blocking request with
   `vmaf_score_pooled` semantics on the backend's own stream or queue, finished
   by a callback or a poll, with results identical to the synchronous call. The
   latency contract is "result within *k* frame-times after the window closes",
   with *k* measured per named device in RC8. There is no per-GOP millisecond
   promise.
6. **Data source.** The closed loop comes first: the encoder's reconstructed
   frame plus the source, since full-reference VMAF needs the reference stream.
   Post-decode monitoring follows through the same import API.
7. **HDR.** PQ-native metrics (PU21, ΔE-ITP; GPU twins are 1.1 work, #1247 and
   #1248) and an HDR model once one exists. Never tone-map to BT.709 and then
   score with an SDR model as an HDR measure. Dolby Vision is passthrough only;
   VMAFx never reads or applies RPU metadata.

Out of scope: a library rename, a CMake build, tone mapping as an HDR measure,
Dolby Vision decoding, a commercial licence.

## Alternatives considered

Decision matrix: each row is one maintainer question and the options offered.

| Question | Option | Pros | Cons | Outcome |
|---|---|---|---|---|
| Q1 licensing for embedding | EUPL-1.2 plus documented embedding rules (**chosen**) | No relicensing; terms already shipped and gated; the EUPL allows linking without conditions beyond source pointers | Some embedders will want a permissive or commercial licence | Chosen |
| Q1 | Dual licence | Gives embedders a non-copyleft route | Valid only for code whose copyright is held by one party; no contributor agreement exists and authorship of AI-assisted files is unsettled | Not chosen |
| Q1 | Commercial licence | Revenue and a simple answer for vendors | Same copyright-holding problem; adds a sales and support obligation | Not chosen |
| Q1 | Relicense parts permissively | Removes copyleft from the embedding surface | Splits the licence story ADR-1250 just unified; Netflix-inherited code cannot move; a header licence does not change the library behind it | Not chosen |
| Q2 commercial encoder SDKs | Target after 1.0 (**chosen**) | Reaches the integrators who need zero-copy; Windows and macOS builds also help every user | Adds platform builds and a CMake package to maintain | Chosen |
| Q2 | Linux-only | Smallest scope | Vendor SDKs ship on Windows and macOS; the API would not be reachable | Not chosen |
| Q2 | No vendor target | No new support surface | Leaves the stated use case unserved | Not chosen |
| Q3 milestone | New post-1.0 milestone (**chosen**) | Keeps RC3 to RC9 and 1.1 scope intact; one place for the epic | One more milestone to curate | Chosen |
| Q3 | Fold into 1.1 | No new milestone | 1.1 is metrics and GPU twins; embedding would compete with them | Not chosen |
| Q3 | No milestone | No process cost | The work has no schedule or exit; it drifts into RC phases | Not chosen |
| Q4 data source | Reconstructed frame first, post-decode second (**chosen**) | A closed loop is the use case; one import API serves both | Needs encoder SDK cooperation first | Chosen |
| Q4 | Reconstructed only | Smallest scope | Leaves monitoring pipelines unserved though the API is the same | Not chosen |
| Q4 | Post-decode only | Works with any encoder, no SDK needed | Monitoring, not a loop: the encoder cannot react | Not chosen |
| API shape | Release callback only | Simple | Races without an ordering primitive (ADR-1199) | Not chosen |
| API shape | New `libvmafx_core` and header family | Clean slate | Second implementation of the engine; breaks the ABI promise; ADR-0686 deferred the rename | Not chosen |
| HDR | Tone-map, then SDR VMAF | Reuses the shipped model | Measures the SDR rendition, not HDR quality | Not chosen |

## Consequences

- **Positive**: embedders get a stated licence position and a defined, additive
  API direction; no embedding work competes with RC3 to RC9.
- **Negative**: the API and its fences are a long-lived ABI commitment;
  Windows and macOS shared-library releases and a CMake package are new
  maintenance; every throughput and latency figure must come from RC8 per
  device before it is published.
- **Neutral / follow-ups**: work items sit under #2067, each with its own ADR
  where a real alternative is chosen, docs in the same PR and failing-first
  tests. Acceptance: bit-identical imported scores on every exact twin, no host
  copy on the import path (backend profiler), figures linked to RC8, and an
  example C project that builds against the installed package on Linux, Windows
  and macOS.

## References

- `req` (verbatim, German): "und ich möchte dass du post rc3 hiermit überarbeitest (heisst nicht dass alles in dem text korrekt ist)"
- `Q1` (licensing for embedding, verbatim answer): "EUPL + documented embedding (Recommended)"
- `Q2` (commercial encoder SDKs as a target, verbatim answer): "Yes, post-1.0 (Recommended)"
- `Q3` (milestone, verbatim answer): "New post-1.0 embedding milestone (Recommended)"
- `Q4` (data source for the quality loop, verbatim answer): "Both, reconstructed first (Recommended)"
- Epic [#2067](https://github.com/VMAFx/vmafx/issues/2067); [milestone 8](https://github.com/VMAFx/vmafx/milestone/8)
- [ADR-1250](1250-eupl-fork-relicense.md), [ADR-1503](1503-tester-artifact-licensing.md), [ADR-1513](1513-production-artifact-licensing.md)
- [ADR-0928](0928-vmaf-picture-v2-explicit-backend-state.md), [ADR-1199](1199-cuda-picture-handover-barrier.md), [ADR-0423](0423-metal-iosurface-import-scaffold.md), [ADR-1121](1121-sycl-qsv-zerocopy-p010-normalization.md), [ADR-1406](1406-cuda-cli-pinned-host-picture-pool.md)
- [ADR-1490](1490-rc3-rc9-candidate-map-cpu-capability.md), [ADR-1341](1341-rc-correctness-benchmark-retrain-sequence.md)
