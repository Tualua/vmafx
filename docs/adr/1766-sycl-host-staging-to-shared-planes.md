<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1766: SYCL extractors that stage luma from host pictures read the shared device planes on both paths

- **Status**: Proposed (post-1.0, [ADR-1685](1685-post-1-0-embedding-zero-copy-milestone.md); umbrella [ADR-1768](1768-sycl-zerocopy-chroma-admission-post-1-0.md))
- **Date**: 2026-10-03
- **Deciders**: lusoris
- **Tags**: `sycl`, `zero-copy`, `correctness`, `fork-local`

## Context

[ADR-1688](1688-sycl-zero-copy-luma-only-admission.md) made every SYCL
extractor that reads a `VmafPicture` refuse zero-copy input with `-ENOTSUP`
(through `vmaf_sycl_require_host_pictures`) instead of crashing or dropping
features silently. Zero-copy input (a VA surface imported straight into device
memory) gives the extractors `NULL` pictures; the luma is already in the shared
planes of [ADR-1369](1369-sycl-shared-planes-light-twins.md).

Several extractors still stage their luma privately: they copy the picture
into a pinned host buffer (`copy_y_plane`), copy that to a private device
buffer, and run their kernel on the private copy. On the host-upload path the
core already writes the same bytes into the shared planes before any extractor
submits (`read_pictures_sycl_prep` calls `vmaf_sycl_shared_frame_upload` every
frame), at the tight pitch `width * bytes_per_sample`. The private copy is
therefore a byte-identical duplicate of a plane that is on the device anyway,
and the guard makes the extractor unusable on exactly the input the fork
optimises for.

The first three (rank 1 in the Phase 12 research) are `float_psnr_sycl`,
`float_motion_sycl` and `float_vif_sycl`. Each is bit-exact against its CPU
extractor on host upload (ADR-1450 for psnr, ADR-1411 for motion, ADR-1422 for
vif), and that contract stays: a kernel that reads the same bytes computes the
same value.

## Decision

Every SYCL extractor that stages luma from host pictures reads the shared
device planes instead, on both paths (host upload and zero-copy), and drops its
`vmaf_sycl_require_host_pictures` guard:

1. `submit` calls `vmaf_sycl_queue_after_upload(state, q)` on the primary
   queue, then passes `vmaf_sycl_get_shared_plane(state, is_ref, 0)` to the
   existing launch function with `raw_stride = width * bytes_per_sample`. The
   private pinned/device luma buffers and the H2D copies go away.
2. The reader stays on the primary queue, the queue
   `sycl_fence_slot_readers` already covers, so a slot is never overwritten
   while a kernel still reads it.
3. Where an extractor needs an owned buffer (it keeps a plane across frames,
   or needs a converted plane), it copies device to device on the primary
   queue instead of reading the slot in place. The chroma readers of the later
   stages (motion chroma, the SpEED pipeline, ssimulacra2) follow this rule.
4. `float_ms_ssim_sycl` gets a small device `plane_to_float` conversion kernel
   in its own feature TU, because its CPU contract converts to float first.
   The kernel lives in the feature TU, which carries the strict-FP flags; it
   is not a generic conversion layer in `common.cpp`.
5. No extractor-local tolerance is added to make a parity case pass. The guard
   row of every migrated extractor in `test_sycl_zerocopy_guards` flips from
   `-ENOTSUP` to "scores" in the same change as its migration.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Read the shared planes on both paths (chosen) | Bytes identical to the private copy, so bit-exactness cannot change; removes a host staging copy and two buffers per extractor; zero-copy works | Every migrated extractor depends on slot fencing being right | Chosen |
| Keep host staging and require host pictures (status quo, ADR-1688) | No code change | Zero-copy stays unusable for these extractors, which is the defect | Not a fix |
| Read the shared planes back to the host and stage from there | Reuses the staging code | Defeats zero-copy: a device to host copy per frame per plane | Throughput regression |
| A generic float conversion layer in `common.cpp` | One conversion for all | `sycl_sources` lack the strict-FP compile line, so conversions there are not bit-exact vs CPU | Wrong FP flags |
| Per-extractor opt-out switch (stay on staging) | Zero risk for the old path | Two code paths to keep bit-exact | Doubles maintenance |

## Consequences

- **Positive**: `float_psnr_sycl`, `float_motion_sycl` and `float_vif_sycl`
  run on zero-copy input and equal both their host-upload output and the CPU
  extractor; fewer device buffers and one fewer copy per frame on the host path.
- **Negative**: the extractors now share the slot-lifetime contract of the
  shared planes. A new reader on another queue would be a correctness bug.
- **Neutral / follow-ups**: plans 12-11 to 12-14 migrate the remaining
  host-staging extractors under the same rule; the guard table shrinks as they
  land.

## References

- `req`: Phase 12 scope, 12-CONTEXT: "migrate host-staging extractors" to
  read the shared planes.
- Phase 12 research ZC-05 table and "Key insight", Pitfall 6.
- Related: [ADR-1369](1369-sycl-shared-planes-light-twins.md),
  [ADR-1395](1395-sycl-kernels-no-scratch.md),
  [ADR-1411](1411-sycl-float-motion-cpu-float-sum.md),
  [ADR-1422](1422-sycl-float-vif-cpu-arithmetic.md),
  [ADR-1450](1450-sycl-float-psnr-exact-block-sums.md),
  [ADR-1688](1688-sycl-zero-copy-luma-only-admission.md),
  [ADR-1765](1765-sycl-zerocopy-planar-chroma-import.md).
- Drafted as ADR-1598 on the Tualua fork; renumbered to 1766 when it was ported to
  `VMAFx/vmafx` as a post-1.0 draft, above the numbers its branches claim (up to 1762).
