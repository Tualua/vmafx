<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1767: `float_motion_sycl` implements `motion_add_uv`, chroma SAD in the CPU's order

- **Status**: Proposed (post-1.0, [ADR-1685](1685-post-1-0-embedding-zero-copy-milestone.md); umbrella [ADR-1768](1768-sycl-zerocopy-chroma-admission-post-1-0.md))
- **Date**: 2026-10-03
- **Deciders**: lusoris
- **Tags**: `sycl`, `zero-copy`, `float-motion`, `option-parity`, `fork-local`

## Context

The CPU `float_motion` extractor has a `motion_add_uv` option (alias `mau`):
the chroma planes are blurred with the same separable Gaussian as luma, at the
subsampled size, and each plane's SAD score is added to the luma score
(`motion_score_pair()` in `core/src/feature/float_motion.c`). The `float_motion_hip`
twin implements it; `float_motion_sycl` did not, so the twin was refused for
that option.

That refusal was harmless on host upload, where the filter and the CLI fall
back to the CPU extractor with a warning. It is a hard failure on zero-copy
input: a CPU extractor cannot run there (ADR-1688), so
`feature=name=float_motion:motion_add_uv=true` on a QSV-decoded pair stops with
`SYCL twin float_motion_sycl cannot honour option 'motion_add_uv'`. The Phase 12
end-to-end harness (`float_motion_uv`) found it; no earlier plan had run the case.

## Decision

`float_motion_sycl` declares `motion_add_uv` (alias `mau`, bool, default false,
`VMAF_OPT_FLAG_FEATURE_PARAM`) at the CPU table's position, between
`motion_blend_offset` and `motion_max_val`, so the aliased feature names equal
the CPU's. With the option on:

1. Cb and Cr are blurred by the luma kernel at their own size from the shared
   chroma planes (`vmaf_sycl_shared_chroma_init` at init;
   `vmaf_sycl_shared_chroma_upload` for host pictures; zero-copy input needs
   `vmaf_sycl_require_chroma`, ADR-1765, and is refused with `-ENOTSUP` when the
   import did not mark the chroma). The blur ping-pong and the row-SAD buffer are
   per plane.
2. Each plane's SAD is the ADR-1411 row kernel (one work-item per row, plain
   left-to-right loop) and the host forms each plane's score with
   `vmaf_float_motion_score_from_row_sads()`, as `compute_motion()` rounds it
   to `float`. The planes are added in `double` in the CPU's order, `Y + U + V`.
3. `motion2`, `motion3`, the blend, `motion_max_val`, `motion_fps_weight` and
   `motion_force_zero` act on that sum exactly as before, so no output name changes.
4. The twin stays free of fp64 and of scratch memory and builds with the SYCL
   strict FP line (ADR-1367, ADR-1395). The result is `==` the CPU's on host
   upload; zero-copy equals host upload.

`motion_add_scale1` and `motion_filter_size` stay undeclared: a request for
them still falls back to the CPU on host upload and fails on zero-copy.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Implement `motion_add_uv` in the twin (chosen) | The e2e case passes with no harness exception; one rule for every option the twin declares; reuses the ADR-1411 kernels per plane | More buffers and one chroma upload per frame with the option on | Chosen |
| B: keep the refusal and make the harness count it as a pass | No code | A "loud-fail" verdict for a stage-3 case; the option stays unusable on zero-copy | Rejected by the user |
| C: drop the `float_motion_uv` case | No code | Hides the gap | Rejected by the user |
| Reduce chroma on the device in another order | Possibly faster | Not the CPU's sum; ADR-1409 shows the order changes the low bits | Breaks `==` |

## Consequences

- **Positive**: `float_motion` with `motion_add_uv=true` runs on the SYCL twin
  on both paths and returns the CPU's scores bit for bit.
- **Negative**: with the option on, two more planes are blurred and summed per
  frame and the extractor reads chroma, so zero-copy needs a chroma-marked import.
- **Neutral**: default (luma-only) behaviour and throughput are unchanged.

## References

- `req`: 12-14 checkpoint, D-10: "A: реализовать сейчас".
- [ADR-1411](1411-sycl-float-motion-cpu-float-sum.md),
  [ADR-1409](1409-float-motion-twins-cpu-float-sum.md),
  [ADR-1688](1688-sycl-zero-copy-luma-only-admission.md),
  [ADR-1765](1765-sycl-zerocopy-planar-chroma-import.md),
  [ADR-1766](1766-sycl-host-staging-to-shared-planes.md).
- Drafted as ADR-1599 on the Tualua fork; renumbered to 1767 when it was ported to
  `VMAFx/vmafx` as a post-1.0 draft, above the numbers its branches claim (up to 1762).
