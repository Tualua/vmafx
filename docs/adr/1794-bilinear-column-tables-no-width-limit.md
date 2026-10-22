<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1794: Bilinear column tables without a width limit

- **Status**: Accepted
- **Date**: 2026-10-05
- **Deciders**: @Lusoris
- **Tags**: `speed`, `vif`, `upstream-divergence`, `performance`, `fork-local`

## Context

Netflix/vmaf `78e11b52c` computes the source columns and weights of bilinear
scaling once per output column instead of once per pixel. SpEED keeps the
table per extractor instance on the heap. The generic
`vif_scale_frame_s(vif_scale_bilinear, ...)`, which `float_vif` uses for its
bilinear prescale, keeps one table of `VIF_BILINEAR_MAX_WIDTH` (7680) entries
on the stack, asserts that the output is not wider, and `float_vif` and
`float_motion` refuse such outputs at init. Before the commit any width was
accepted. The fork has to take the arithmetic unchanged (it is the per-pixel
arithmetic, column by column) but chooses how the generic path stores its
table.

## Decision

We will walk the output columns of the generic path in chunks of 1024
(`VIF_BILINEAR_COLUMN_CHUNK`): fill the table for one chunk on the stack,
apply it to every row, move to the next chunk. Every width scales with the
per-pixel scaler's bits, so neither the assert nor the init refusals are
ported. SpEED's per-instance table is ported as upstream has it.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Chunked stack table, no limit (chosen) | Same bits for every width; no allocation per frame; no new failure | A loop over chunks upstream does not have | — |
| Upstream's 7680-entry stack table, assert, init refusals in `float_vif` / `float_motion` | Same code and same refusals as upstream | `float_vif` with a bilinear prescale refuses outputs wider than 7680 that it scores today; a release build (no assert) writes past the stack table for any caller that forgets the check | Turns working inputs into errors and leaves an unchecked bound |
| Allocate the table per call on the heap | No limit, no chunk loop | One allocation per frame in the scoring path (HISS-03) | Allocation in a hot loop |

## Consequences

- **Positive**: bilinear prescales of SpEED and `float_vif` cost less (SpEED's
  `speed_chroma` 4.8 instead of 11.2 ms per 3840x2160 frame); no input the
  fork accepted before is refused.
- **Negative**: the fork accepts bilinear outputs wider than 7680 that upstream
  refuses; the upstream parity guard compares only runs both trees accept, so
  no allowlist row is needed.
- **Neutral / follow-ups**: `core/test/test_vif_bilinear.c` holds both paths to
  the per-pixel scaler; the rebase note forbids importing the macro, the
  assert and the refusals on a sync.

## References

- Netflix/vmaf `78e11b52c` ("libvmaf/speed_chroma: remove bilinear prescale
  index/weight computation from per-pixel loop").
- Upstream coverage sync of 2026-10-05 (`port/78e11b52c-speed-bilinear-columns`).
