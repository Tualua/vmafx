<!-- markdownlint-disable MD013 MD060 -->
# ADR-1769: Throughput of the SYCL zero-copy path on Arc A380: measured limits and candidate scope

- **Status**: Accepted
- **Date**: 2026-10-06 (accepted 2026-10-07)
- **Deciders**: Lusoris Dev (selected by user, 2026-10-06)
- **Tags**: sycl, zero-copy, performance, arc-a380, profiling, fork-local

## Context

Phase 13 of the roadmap asks to "Raise the throughput of FFmpeg `libvmaf_sycl` on QSV zero-copy input (Arc A380, 3840x1600 10-bit HDR10 HEVC vs AV1, currently ~42-46 fps with Compute ~97%, Blitter ~90%, one CPU core at 100% in busy-wait) by removing avoidable work, without changing any score bit". The baseline of [Research-1769](../research/1769-sycl-zerocopy-throughput-a380.md) measured the three models on a 200-frame real-content segment:

- A frame takes 22 ms (45 fps). The compute engine is busy 19.7 ms of it. The filter is compute-bound.
- A VTune xpu-offload run splits the 19.9 ms of GPU tasks into VIF 11.0 ms (`IntegerVifHoriKernel<0>` alone 6.3 ms), ADM 6.3 ms, motion 1.25 ms, import 1.1 ms; about 37 launches per frame leave 2.2 ms of gaps.
- Steady state is 45.3 fps (22.09 ms/frame), from 600 and 20 frame runs.
- The Blitter busy time (18.4 ms/frame) follows the feature kernels, overlaps the compute engine and is not on the critical path; it is not the decoder (0 in a decode-only run) and not the import (0.86 ms). The exported modifier is plain Tile4.
- The host waits for the GPU 89 % of each frame; the spin costs about one core and buys no throughput.
- A 200-frame run is about 12 s of process start-up; the harness fps (12.3) is start-up-diluted, so a gain of the frame shows at about a quarter of its size.
- The NEO debug keys and the immediate command-list mode do not lower the spin and are slower; graph dispatch is neutral. All rows are bit-identical.
- Zero-copy `n_subsample` above 1 returns wrong motion (`T-SYCL-ZEROCOPY-NSUBSAMPLE-MOTION-2026-10-06`).

Constraint: no score bit may move ([ADR-1121](1121-sycl-qsv-zerocopy-p010-normalization.md), the exact-twin gates, the Netflix golden gate). [ADR-1596](1596-sycl-va-import-immediate-cmdlist.md) rules out import caching and delayed frees; [ADR-1597](1597-sycl-zerocopy-planar-chroma-import.md) fixes the chroma import; [ADR-1369](1369-sycl-shared-planes-light-twins.md) and [Research-1395](../research/1395-sycl-kernels-no-scratch.md) fix the shared-plane and scratch rules.

## Decision

The user selected the candidates in plan 13-02. The kernel ranks give the order of work.

Selected for implementation: K1 (VIF scale-0, rank 1), K5 (merge scale-2/3 launches, rank 2), K3 (16-byte de-tile, rank 3), C3 (device fence on the VA import, dropping the frame-start wait).

Not pursued: C1, C2, K4 (by decision); K2 (not selected).

C3 starts only after the zero-copy `n_subsample` motion defect (`T-SYCL-ZEROCOPY-NSUBSAMPLE-MOTION-2026-10-06`) is fixed, which the user chose to do first. The fix is in: `vmaf_sycl_graph_skip()` lets a frame with skipped extractors run the extractors that submitted.

Design of C3:

- Today the filter calls `vmaf_sycl_wait_compute()` at the start of every frame. That host wait drains the primary and the combined queue before the VA import overwrites the upload slot, and it is the only thing that protects the slot on the import path.
- The import instead orders its writes on the device. `sycl_fence_slot_readers()`, the slot fence the host upload already uses ([ADR-1369](1369-sycl-shared-planes-light-twins.md)), takes the queue to barrier as a parameter. The host upload passes the copy queue, as before. The VA import passes the primary queue, where the de-tile, the chroma de-interleave and the readback copies run, so the import of frame N waits on the device for every reader of frame N-2's slot.
- The markers of a retiring slot cover the primary queue, the combined queue and every per-extractor compute queue. `vmaf_sycl_create_compute_queue()` keeps a copy of each queue it creates in the state; a `sycl::queue` is a counted handle, so the copy stays valid after the extractor deletes its own. An extractor that `n_subsample` skipped and that nothing collected is still covered.
- `vmaf_sycl_fence_import_slot()` runs the fence once per frame, at the start of `vmaf_sycl_import_va_surface()`, before any write to the slot on any path (DMA-BUF import or readback).
- The barrier is only submitted when a marker is still pending, so a frame whose readers finished costs one status query per marker.
- Patch 0005 calls `vmaf_sycl_wait_compute()` on the host-upload branch only. The QSV branch relies on the fence. `vmaf_read_pictures_sycl()` still waits on the primary queue before the frames go back to FFmpeg, so the surfaces and the deferred import frees keep their order ([ADR-1596](1596-sycl-va-import-immediate-cmdlist.md)). The public `vmaf_sycl_wait_compute()` keeps its documented meaning.
- No data path changes, so no score bit moves. Validation on the device (CPU identity at `n_subsample` 1, 2 and 4, graph replay, `--repeat 10` batched) and the A/B timing come in plan 13-06.

Acceptance uses the steady-state fps of 600 and 20 frame runs (45.3 fps baseline).

Outcome of the device validation and A/B (plan 13-06, numbers in [Research-1769](../research/1769-sycl-zerocopy-throughput-a380.md#ab-results-plan-13-06)):

- K1 is kept: the scale-0 VIF horizontal pass reads a local-memory tile; the frame is 1.16 ms shorter (22.33 to 21.17 ms of filter time) and bit-identical.
- K3 is reverted: 0.05 ms per frame, inside the noise. Its byte-for-byte device test stays.
- C3 is reverted with the device slot fence: the host wait moved from the frame start into the VA import and neither fps nor host CPU changed. The design above is therefore not in the tree; the filter keeps its frame-start `vmaf_sycl_wait_compute()`.
- K5 was not coded: every scale-2/3 launch depends on the one before it.

Final outcome (plans 13-09 and 13-07, numbers in [Research-1769](../research/1769-sycl-zerocopy-throughput-a380.md#final-acceptance-plan-13-07)):

| Candidate | Outcome | Measurement |
| --- | --- | --- |
| K1, VIF scale 0 | Kept (`4969caff5`) | Kernel 6.32 to 5.33 ms per frame (VTune); filter GPU time -1.16 ms per frame; bit-identical |
| K1-s1, VIF scale 1 | Rejected, reverted (`aebcf69bb`) | GPU ms 21.27 to 21.19, inside the 0.15 ms range of the rows; kernel 1.276 to 1.165 ms (VTune) |
| K1-s23, VIF scales 2 and 3 | Rejected, reverted (`46e1f72fb`) | GPU ms 21.16 against 21.19, inside the range; kernels unchanged (0.306 and 0.071 ms) |
| K3, 16-byte de-tile | Rejected, reverted | -0.05 ms per frame, inside the spread; failed its own device test before a fix |
| C3, device slot fence | Rejected, reverted | No fps gain, host CPU 24.8 to 24.6 ms per frame; the host wait moved into the VA import |
| K5, merged launches | Abandoned before coding | No independent launch worth more than about 0.1 ms |
| C1, C2, K4 | Skipped by decision | Not coded |
| K2, ADM | Not selected | Not coded |

At the final head against the pre-phase library, video only: `vmaf_v0.6.1` 43.9 to 45.9 steady fps (+4.5 %, GPU time 22.60 to 21.58 ms), `vmaf_4k_v0.6.1` 43.6 to 46.2 fps (+5.9 %, 22.66 to 21.44 ms), the whole 76378-frame episode with `vmaf_4k_v0.6.1` 44.99 to 47.44 fps (+5.4 %), every frame identical and the pooled score 95.454927 on both. `vmaf_v1.0.16_3d0h` went from 50.9 to 41.8 fps (-17.8 %) because its SpEED covariance is now exact ([ADR-1931](1931-sycl-speed-covariance-fast-exact.md)).

The 15 % target is not met: 15 % needs 19.8 ms per frame and the final head runs at 21.8 ms. The compute engine is busy 96 % of the frame, so the overlap candidates have no idle time left to use; the remaining ceilings are ADM arithmetic (K2, up to 7 %) and launch merging (K5, up to 4 %), neither attempted. The digest's final section is the measured irreducibility record. User guidance is in [Throughput on Arc A-series](../backends/sycl/zero-copy.md#throughput-on-arc-a-series-adr-1769) and [speed against accuracy](../usage/ffmpeg.md#speed-against-accuracy-with-libvmaf_sycl).

## Alternatives considered

Ceilings are upper bounds from the baseline, in ms of the 22.1 ms frame.

| Option | Pros | Cons | Measured ceiling | Why chosen or not |
| --- | --- | --- | --- | --- |
| C2 low-power host wait | frees about one host core; no data-path risk | no fps gain; wake-up latency may cost up to 1 % | 0 ms | not selected |
| C3 device slot fence on the VA import, drop the frame-start wait | import overlaps compute; one spin fewer | bounded by the 2.2 ms gap; patch 0005 edit; every reader queue must be fenced; needs the `n_subsample` defect fixed first | 2.2 ms (3 to 10 %) | selected, after the `n_subsample` fix |
| C1 skip chroma import for luma-only models | removes chroma de-interleave | new extractor callback; amends ADR-1597 D-01 | 0.3 ms (about 1 %) | not selected |
| K3 de-tile 16 bytes per work-item | fewer work-items for a copy plus shift | small | 0.2 ms (1 %) | selected, rank 3 |
| K1 VIF scale-0 intermediates (SLM-tiled vertical plus horizontal) | largest possible kernel gain | large effort; must be `==` at 4K and scratch-free; the horizontal kernel is arithmetic-bound | about 2.4 ms (up to 11 %) | selected, rank 1; kept (-1.16 ms) |
| K1 extended to VIF scales 1-3 (plan 13-09) | same kernel, no new arithmetic | the scales are small (1.3 ms and 0.4 ms of the frame) | about 0.2-0.3 ms | tried; no frame-level gain beyond the range, reverted |
| K2 ADM hotspot | targets 6.3 ms | no hotspot above 7 % of the frame; row rounding must stay whole (ADR-1167) | 6.3 ms, no task above 7 % | not selected |
| K4 dispatch or env default | zero code | measured neutral or slower | 0 ms | rejected by the env rows |
| K5 merge tiny scale-2/3 launches | integer, order-free | 37 launches per frame; shares the idle gap with C3 | about 0.8 ms (up to 4 %) | selected, rank 2 |
| Irreducibility write-up only | honest if ceilings are small; cheap | no throughput gain | 0 | not needed: candidates selected |

Excluded: import caching, delayed frees, float reorder, copy-engine imports, `vif_fused` advice, NEO debug keys as a default.

## Consequences

- **Positive**: later plans act on measured costs; the start-up artifact of the 200-frame benchmark and the `n_subsample` defect are found before code lands.
- **Negative**: the 15 % target may be out of reach; acceptance numbers need a long segment or the steady-state formula.
- **Neutral / follow-ups**: `T-SYCL-ZEROCOPY-NSUBSAMPLE-MOTION-2026-10-06` is fixed; the user guidance (`n_subsample`, model choice, start-up) is on the zero-copy and FFmpeg pages; the next throughput work is ADM arithmetic (K2) or launch merging (K5), and the remaining cost of the exact SpEED covariance (`T-SYCL-SPEED-COV-EXACT-SEQUENTIAL-COST-2026-10-06`).

## References

- Phase 13 roadmap entry (goal quoted above); requirement PERF-01.
- [Research-1769](../research/1769-sycl-zerocopy-throughput-a380.md)
- [ADR-1121](1121-sycl-qsv-zerocopy-p010-normalization.md), [ADR-1369](1369-sycl-shared-planes-light-twins.md), [ADR-1596](1596-sycl-va-import-immediate-cmdlist.md), [ADR-1597](1597-sycl-zerocopy-planar-chroma-import.md), [Research-1395](../research/1395-sycl-kernels-no-scratch.md)
- Candidate selection: req (user answers «K1 VIF scale-0, K5 слияние запусков, K3 де-тайлинг 16 байт, C3 без ожидания в начале кадра» and «Чинить первым (Recommended)» for the defect).
