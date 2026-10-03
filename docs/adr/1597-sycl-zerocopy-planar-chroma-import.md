<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1597: SYCL zero-copy imports the VA surface's Cb/Cr into the shared planar chroma planes with one layout-addressed kernel

- **Status**: Accepted
- **Date**: 2026-10-03
- **Deciders**: lusoris
- **Tags**: `sycl`, `zero-copy`, `dmabuf`, `chroma`, `correctness`, `fork-local`

## Context

`vmaf_sycl_import_va_surface` (`core/src/sycl/dmabuf_import.cpp`) reads only
`desc.layers[0]` (luma) of the exported DRM PRIME2 descriptor. After ADR-1595
every extractor that needs chroma (`psnr`, `psnr_hvs`, `ciede`, `motion_add_uv`,
`speed_chroma`, `speed_temporal`, `ssimulacra2`) fails loudly with `-ENOTSUP` on
zero-copy input, because the shared chroma planes of ADR-1369
(`SyclPlaneState::ref[2][2]` / `dis[2][2]`, planar Cb and Cr at the luma bit
depth, tight pitch) are never written on the VA path. Phase 12 Stage 2 imports
the chroma so those extractors run on QSV-decoded frames.

Three facts shape the design.

- **Measured descriptors** (Arc A380, i915, `vaExportSurfaceHandle` with
  `DRM_PRIME_2 | SEPARATE_LAYERS`; evidence
  `12-06-descriptor-probe.log`, digest
  [Research-1595](../research/1595-sycl-zerocopy-feature-correctness.md)): for
  NV12 and P010, at 576x324 and 1920x1080, `num_layers=2 num_objects=1`, both
  layers in object 0, `layers[1].offset` at a tile boundary after the luma
  rows padded to 32, `layers[1].pitch == layers[0].pitch`, one modifier for
  both (Tile4 `0x0100000000000009` in all four probes). `layers[1].drm_format`
  is `GR88` (NV12) or `GR1616` (P010). The pitch can exceed the row bytes
  (P010 1920 wide: row 3840 B, pitch 3968 B; NV12 576 wide: row 576 B, pitch
  640 B).
- **Frame 0.** SYCL extractors initialise lazily after frame 0's import
  (`libvmaf.c`), so an allocation inside extractor `init` would leave frame 0
  without chroma.
- **Luma de-tile code is proven and shared with the P010 shift** (ADR-1121,
  ADR-1129 lesson from the old branch: cloning four de-tile branches with
  chroma heights doubled the surface to maintain).

## Decision

We will import chroma on every zero-copy frame, unconditionally (locked decision
D-01):

1. `vmaf_sycl_init_frame_buffers` calls `vmaf_sycl_shared_chroma_init` eagerly
   with `((w+1)/2, (h+1)/2)`, so the planes exist before frame 0's import.
   There is no "chroma needed" gate.
2. `vmaf_sycl_import_va_surface` imports `layers[1]` of the same DMA-BUF
   object with one new, layout-addressed kernel, `chroma_import.cpp`, in a TU
   that does not include libva (it is unit-testable without a decoder). The
   kernel reads the interleaved UV pairs (`2 * bytes-per-sample` bytes per
   chroma sample) from a LINEAR, Tile4 or Y-tiled surface and writes planar Cb
   and Cr at the luma bit depth into the upload slot of the ADR-1369 planes.
   For P010 the `>> (16 - bpc)` shift is applied exactly once, inside this
   kernel; `launch_p010_normalize` never runs on chroma planes.
3. The existing luma de-tile kernels stay untouched.
4. The readback fallback reuses the kernel with LINEAR layout over the mapped
   host copy's device upload, so all three paths share one chroma conversion.
5. Frame currency: the import sets a pending flag; `vmaf_sycl_advance_frame`
   promotes it to `planes.frame` after the frame counter increments, so
   readers see the planes as current for the frame that was just imported. The
   chroma event is chained into `last_detile_event`.
6. Descriptor validation runs before any device access
   (`vmaf_sycl_chroma_src_validate`): `layers[1]` present; object index below
   `num_objects`; modifier one of LINEAR, Tile4, Y-tiled (anything else reads
   back); tiled `pitch % 128 == 0`; `cw`, `ch` non-zero, `bpc` in
   {8, 10, 12, 16}; the extent in `size_t` arithmetic within the object size:
   LINEAR `offset + (ch-1)*pitch + cw*2*bps`, tiled
   `offset + ceil(ch/32) * (pitch/128) * 4096`. A malformed chroma layer
   returns an error and does not fall back silently with chroma missing.

Scope: Linux VA-API DMA-BUF and readback paths only. The Windows D3D11 import
(`d3d11_import.cpp`) stays luma-only; its chroma readers keep failing loudly
through the ADR-1595 guards. That is out of scope for Phase 12.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Eager allocation, unconditional per-frame chroma import with one new kernel (chosen) | Frame 0 has chroma; one kernel for every layout; matches ADR-1369 planar planes | About half the luma bytes of extra device work per frame; ~49 MB extra at 4K P010 | Chosen (D-01); a gate can follow once 12-09 measures the cost |
| Lazy allocation gated on a chroma-needing extractor | No cost when unused | Extractor init runs after frame 0's import, so frame 0 has no chroma | Wrong scores for frame 0 |
| One interleaved chroma buffer, host-side de-interleave (old branch Option B, ADR-1129) | Smaller kernel | Does not match the planar ADR-1369 planes; every reader would need a de-interleave | Mismatch with the shared-plane design |
| Clone the four luma de-tile branches with chroma height | Reuses the proven shape | Four more kernels to keep in step, plus the P010 shift duplicated; ADR-1129 lesson | Maintenance cost |
| Env off-switch for chroma import now | Zero cost when disabled | Adds a surface before any measurement shows a cost | Deferred until 12-09 measures |

## Consequences

- **Positive**: every extractor that reads shared chroma works on zero-copy
  input, from frame 0, bit-exactly (the kernel is integer-only).
- **Negative**: one extra light kernel and about half the luma bytes of
  memory traffic per imported surface; the chroma de-tile address math is a
  second copy of the Tile4 and Y-tiled swizzle, kept honest by the host-synthesised
  vector test (`test_sycl_chroma_import`).
- **Neutral / follow-ups**: 12-07 adds the runtime (eager init, pending flag,
  upload-slot getters); 12-08 wires the import; 12-09 measures the cost and
  decides on an off-switch. D3D11 chroma is a separate backlog item.

## References

- `req`: D-01, 2026-10-02 (user): allocate shared chroma eagerly in
  `vmaf_sycl_init_frame_buffers`, import `layers[1]` on every zero-copy frame,
  no gating.
- Phase 12 research `## Open Questions (RESOLVED)` Q1 (resolved to D-01) and Q6
  (D3D11 chroma out of scope).
- Probe evidence: four descriptor shapes recorded in
  [Research-1595](../research/1595-sycl-zerocopy-feature-correctness.md)
  "Chroma descriptor probe (A380)".
- Related: [ADR-1121](1121-sycl-qsv-zerocopy-p010-normalization.md),
  [ADR-1369](1369-sycl-shared-planes-light-twins.md),
  [ADR-1595](1595-sycl-zerocopy-fail-loud-twin-routing.md), [ADR-1395](1395-sycl-kernels-no-scratch.md),
  [ADR-0220](0220-sycl-fp64-fallback.md); old-branch ADR-1129 (chroma de-interleave
  site, not in this tree) for the descriptor prior art.
