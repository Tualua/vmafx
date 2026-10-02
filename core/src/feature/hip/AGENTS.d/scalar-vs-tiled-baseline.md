---
paths:
  - core/src/feature/hip/integer_adm_hip.c
  - core/src/feature/hip/integer_cambi_hip.c
invariant: Scalar-per-thread implementation is the correctness baseline prior to templated tiled optimization.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# Scalar-per-thread is the correctness baseline; templated tiled is the perf goal

Porting CUDA twin to HIP -> write kernel scalar-per-thread first (no
shared-memory tiling, no warp reductions), confirm cross-backend
parity at `places=4` (ADR-0214) on Netflix golden pair *before*
porting perf optimisations. HIP wavefront sizes differ between RDNA
(32) and GCN/CDNA (64); warp-reduce path needs own tuning even after
scalar kernel is bit-exact.

**Boundary condition invariant (ADR-1103)**: all filter-loop boundary
reads must use `mirror2_i(idx, dim)` (two-bounce symmetric reflect),
**not** `clamp_i(idx, 0, dim-1)` (replicate-edge). CPU reference uses
`PADDING_SQ_DATA` (symmetric reflect at 0 and dim-1); `clamp_i`
disagrees with this for `filter_half_width` pixels at each edge,
producing places~2.75 gap (max |HIP−CPU| ≈ 0.0018) that violates
ADR-0214. `mirror2_i` already defined in
`integer_vif/vif_statistics.hip`; copy or re-derive it in any new HIP
filter kernel before adding boundary reads.

Established precedent: ADR-0537 ports
`integer_vif/vif_statistics.hip` scalar-per-thread (~540 lines vs
CUDA twin's ~850), accepts ~5–10× perf regression vs CUDA in
exchange for verifiable kernel surface. Perf optimisation deferred to
follow-up ADR.
