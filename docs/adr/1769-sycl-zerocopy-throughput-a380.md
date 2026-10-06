<!-- markdownlint-disable MD013 MD060 -->
# ADR-1769: Throughput of the SYCL zero-copy path on Arc A380: measured limits and candidate scope

- **Status**: Proposed
- **Date**: 2026-10-06
- **Deciders**: Lusoris Dev (selection pending)
- **Tags**: sycl, zero-copy, performance, arc-a380, profiling, fork-local

## Context

Phase 13 of the roadmap asks to "Raise the throughput of FFmpeg `libvmaf_sycl` on QSV zero-copy input (Arc A380, 3840x1600 10-bit HDR10 HEVC vs AV1, currently ~42-46 fps with Compute ~97%, Blitter ~90%, one CPU core at 100% in busy-wait) by removing avoidable work, without changing any score bit". The baseline of [Research-1769](../research/1769-sycl-zerocopy-throughput-a380.md) measured the three models on a 200-frame real-content segment:

- A frame takes 22 ms (45 fps). The compute engine is busy 19.7 ms of it. The filter is compute-bound.
- GPU time splits VIF 12.1 ms, ADM 6.0 ms, VA import and fixed work 2.5 ms, motion 1.2 ms (derived from isolation runs; VTune was not approved).
- The Blitter busy time (18.4 ms/frame) follows the feature kernels, overlaps the compute engine and is not on the critical path; it is not the decoder (0 in a decode-only run) and not the import (0.86 ms). The exported modifier is plain Tile4.
- The host waits for the GPU 89 % of each frame; the spin costs about one core and buys no throughput.
- A 200-frame run is about 12 s of process start-up; the harness fps (12.3) is start-up-diluted, so a gain of the frame shows at about a quarter of its size.
- The NEO debug keys and the immediate command-list mode do not lower the spin and are slower; graph dispatch is neutral. All rows are bit-identical.
- Zero-copy `n_subsample` above 1 returns wrong motion (`T-SYCL-ZEROCOPY-NSUBSAMPLE-MOTION-2026-10-06`).

Constraint: no score bit may move ([ADR-1121](1121-sycl-qsv-zerocopy-p010-normalization.md), the exact-twin gates, the Netflix golden gate). [ADR-1596](1596-sycl-va-import-immediate-cmdlist.md) rules out import caching and delayed frees; [ADR-1597](1597-sycl-zerocopy-planar-chroma-import.md) fixes the chroma import; [ADR-1369](1369-sycl-shared-planes-light-twins.md) and [Research-1395](../research/1395-sycl-kernels-no-scratch.md) fix the shared-plane and scratch rules.

## Decision

Pending user selection of the candidates below (Task 4 of plan 13-02). This ADR will be amended to "Selected for implementation: <list>" by the first code plan. If no candidate is selected, the outcome is the measured irreducibility write-up and user guidance.

## Alternatives considered

Ceilings are upper bounds from the baseline, in ms of the 22.1 ms frame.

| Option | Pros | Cons | Measured ceiling | Why chosen or not (pending) |
| --- | --- | --- | --- | --- |
| C2 low-power host wait | frees about one host core; no data-path risk | no fps gain; wake-up latency may cost up to 1 % | 0 ms | pending |
| C3 device slot fence on the VA import, drop the frame-start wait | import overlaps compute; one spin fewer | bounded by the 2.4 ms idle gap; patch 0005 edit; every reader queue must be fenced; needs the `n_subsample` defect fixed first | 2.4 ms (3 to 10 %) | pending |
| C1 skip chroma import for luma-only models | removes chroma de-interleave | new extractor callback; amends ADR-1597 D-01 | 0.3 ms (about 1 %) | pending |
| K3 de-tile 16 bytes per work-item | fewer work-items for a copy plus shift | small | 0.1 to 0.5 ms (0.5 to 2 %) | pending |
| K1 VIF scale-0 intermediates (SLM-tiled vertical plus horizontal) | largest possible kernel gain | large effort; must be `==` at 4K and scratch-free; VIF is 10 ms above the bandwidth roof, so arithmetic dominates | 1.9 ms (up to 9 %) | pending |
| K2 ADM hotspot | targets 6.0 ms | no per-kernel data; row rounding must stay whole (ADR-1167) | not rated | pending |
| K4 dispatch or env default | zero code | measured neutral or slower | 0 ms | rejected by the env rows |
| K5 merge tiny scale-2/3 launches | integer, order-free | launch count not measured; shares the idle gap with C3 | up to 2.4 ms (10 %) | pending |
| Irreducibility write-up only | honest if ceilings are small; cheap | no throughput gain | 0 | pending |

Excluded: import caching, delayed frees, float reorder, copy-engine imports, `vif_fused` advice, NEO debug keys as a default.

## Consequences

- **Positive**: later plans act on measured costs; the start-up artifact of the 200-frame benchmark and the `n_subsample` defect are found before code lands.
- **Negative**: the 15 % target may be out of reach; acceptance numbers need a long segment or the steady-state formula.
- **Neutral / follow-ups**: fix `T-SYCL-ZEROCOPY-NSUBSAMPLE-MOTION-2026-10-06`; amend this ADR after the selection; document user guidance (`n_subsample`, model choice, start-up) under `docs/backends/sycl/`.

## References

- Phase 13 roadmap entry (goal quoted above); requirement PERF-01.
- [Research-1769](../research/1769-sycl-zerocopy-throughput-a380.md)
- [ADR-1121](1121-sycl-qsv-zerocopy-p010-normalization.md), [ADR-1369](1369-sycl-shared-planes-light-twins.md), [ADR-1596](1596-sycl-va-import-immediate-cmdlist.md), [ADR-1597](1597-sycl-zerocopy-planar-chroma-import.md), [Research-1395](../research/1395-sycl-kernels-no-scratch.md)
- Candidate selection: req (user, Task 4 of plan 13-02), pending.
