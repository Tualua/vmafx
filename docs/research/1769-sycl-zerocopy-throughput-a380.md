<!-- markdownlint-disable MD013 MD060 -->
# Research-1769: Where the SYCL zero-copy time goes on an Arc A380

- **Status**: Active (baseline measured; candidates selected)
- **Workstream**: [ADR-1769](../adr/1769-sycl-zerocopy-throughput-a380.md), phase 13
- **Last updated**: 2026-10-06

## Question

FFmpeg's `libvmaf_sycl` filter on QSV-decoded frames of a 3840x1600 10-bit pair
runs at about 42 to 46 fps on an Intel Arc A380, with the Compute engine near
saturation, the Blitter busy and one host core spinning. Which of those costs
is real, which is on the critical path, and what is each optimisation
candidate worth before any code is written?

## Short answer

The filter is compute-bound. A frame takes 22 ms (45 fps) and the compute
engine is busy for 19.7 ms of it (89 %). VIF is 55 % of the GPU time, ADM 27 %,
the VA import and its fixed work 11 %, motion 5 %. The Blitter is not decode
and not the import: it follows the feature kernels, runs beside them and is not
on the critical path. The host spin is real (about one core) but buys no
throughput back. No candidate is worth more than about 10 % of the frame on
its own, so the 15 % target needs a kernel candidate to succeed or the
irreducibility write-up applies. One finding is a defect: zero-copy
`n_subsample` above 1 returns wrong motion (see [Defect found](#defect-found)).

A second finding changes how every later number must be read: a 200-frame
segment is mostly start-up. See [Start-up dominates the segment](#start-up-dominates-the-segment).

## Setup

| Item | Value |
| --- | --- |
| Device | Intel Arc A380 (0x56a5), i915 (`drm-driver: i915` in fdinfo), host kernel 6.19.14-200.fc43 |
| Runtime | Level Zero `libze-intel-gpu1` 26.35.39758.10, oneAPI icx / icpx image `localhost/vmafx:build-ocloc` |
| Builds | base `41efb8a10` (frozen "before"), branch `perf/sycl-zerocopy-throughput` (adds the env-gated `VMAF_SYCL_TIMING` timers); library code of the two differs only by those timers |
| Dev build note | `-Dsycl_icpx_aot_targets=` is empty, so kernels are JIT-loaded in every process; shipped builds compile ahead of time |
| Content | `Young.Sherlock.S01E01` 2160p: HEVC HDR10 reference against its AV1 encode, both decoded by QSV, 3840x1600 P010, first 200 frames (`trim=end_frame=200` on both inputs) |
| Repeats | one unrecorded warm-up leg, then R = 3; spread gate rejects a row whose min and max differ by more than the tool's limit |

Commands (from [Throughput harness](../backends/sycl/overview.md)): every row
is `scripts/test/zerocopy-throughput.sh --frames 200 --repeat 3 --warmup 1
--model <m> --label <base|branch>`, with `--build-root` pointing at the frozen
base for `base`; ladder rows add `--ladder L0|L1|L2`; env rows add `--env
K=V`; `scripts/test/zerocopy_throughput_report.py summarize` and `same-scores`
read the output. The fps of an instrumented run (`VMAF_SYCL_TIMING`,
`VMAF_SYCL_PROFILE`) is never quoted.

## Start-up dominates the segment

A 5-frame L1 run takes 11.8 s and 29 s of host CPU; the 200-frame L1 run takes
13.2 s. So a 200-frame run is about 12 s of process start-up plus the frames.
`fps = 200 / rtime` is therefore start-up-diluted: it reads 12.3 while the
filter's own per-frame time reads 45 fps. Steady state is
`(rtime(N) - rtime(20)) / (N - 20)` from two runs of one session (calibrated below). The remaining columns below use the steady state
unless a column says "harness fps". Consequence for the phase: a 15 % gain of
the frame shows as about 4 % on the 200-frame harness fps, so acceptance runs
need a long segment (`--frames 0`) or the steady-state formula.

## Per-model baseline (R = 3 medians, un-instrumented)

| Model | Build | Harness fps | Steady ms/frame | Filter gpu ms | Compute busy ms | Copy busy ms | Video busy ms |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `vmaf_v0.6.1` | base | 12.25 | 22.2 | 21.82 | 19.61 | 18.25 | 3.16 |
| `vmaf_v0.6.1` | branch | 12.31 | 22.1 | 21.82 | 19.78 | 18.42 | 4.38 |
| `vmaf_4k_v0.6.1` | base | 12.32 | 22.0 | 21.85 | 19.69 | 18.34 | 2.77 |
| `vmaf_4k_v0.6.1` | branch | 12.39 | 21.6 | 21.83 | 19.89 | 18.51 | 4.54 |
| `vmaf_v1.0.16_3d0h` | base | 12.62 | 20.1 | 19.98 | 18.53 | 17.18 | 3.48 |
| `vmaf_v1.0.16_3d0h` | branch | 12.54 | 20.6 | 19.97 | 18.52 | 17.15 | 3.83 |

Base against branch is the noise floor: under 1 % on every model, and the
scores are `IDENTICAL` between them (200 frames, `%.17g`). The two v0.6.1
models have the same GPU cost, as the model map predicts. Host CPU is 171
ms/frame on the harness, of which about 145 ms/frame (29 s) is start-up.

## Engines

Counters are the DRM fdinfo `drm-engine-*` values summed over every DRM client
of the ffmpeg process (decoder client plus SYCL client; before the report tool
summed them it read 0 % for the SYCL client).

| Row | Compute ms/frame | Copy ms/frame | Video ms/frame |
| --- | --- | --- | --- |
| L0 decode only (800 frames) | 0 | 0 | 48 % busy |
| L1 import + `motion` | 1.90 | 0.86 | 0.66 |
| `vif` alone (+ import) | 12.70 | 11.34 | 3.84 |
| `adm` alone (+ import) | 6.57 | 5.28 | 4.53 |
| `motion` alone (+ import) | 1.93 | 0.89 | 1.07 |
| L2 `vmaf_v0.6.1` | 19.64 | 18.26 | 4.42 |

Per frame the compute engine is busy 19.7 of 22.1 ms and the copy engine 18.4
ms. The decoder does 2.3 ms of work per frame and never limits the pair (428 fps
alone).

## Attribution ladder and the Blitter question (H-BLT)

| Level | What runs | Harness fps | Steady ms/frame | Filter gpu ms |
| --- | --- | --- | --- | --- |
| L0 | decode both inputs, null sink | 428.5 (800 frames) | 2.3 | n/a |
| L1 | L0 + import + `motion` only | 15.21 | 6.4 | 3.77 |
| L2 | `vmaf_v0.6.1` | 11.77 | 22.7 | 21.86 |

Import and de-tile cost 2.5 ms per frame (derived below); the features cost 19
ms. The exported surface's modifier is `0x0100000000000009` on every frame of
both inputs, `I915_FORMAT_MOD_4_TILED` (plain Tile4) in `drm_fourcc.h`; it is
not one of the media-compressed variants, so the driver has no resolve to do on
export.

H-BLT answer, by measurement: the copy engine is not the decoder (L0 copy = 0)
and not the import (L1 copy 0.86 ms). Its busy time scales with the feature
kernels (`vif` 11.3, `adm` 5.3, `motion` 0.9 ms) at 80 to 93 % of the compute
busy time, and the sum of compute and copy (38 ms) exceeds the 22 ms frame, so
the two engines overlap. The filter's own GPU time (21.8 ms) equals the compute
busy time, so the Blitter is not on the critical path. What issues the
Blitter work inside a kernel launch sequence is not visible without a
per-kernel profile (the VTune list below shows only `zeCommandListAppendMemoryCopy`
and `Fill` of the copy type, 0.2 ms per frame). Reducing it frees no frame time
on this evidence.

## Host phases

With `VMAF_SYCL_TIMING=1` (one run per model, not an fps source), host
milliseconds per frame:

| Model | `queue_wait` | `combined_wait` | `graph_wait` | VA import |
| --- | --- | --- | --- | --- |
| `vmaf_v0.6.1` | 0.86 | 19.38 | 0.11 | 0.78 |
| `vmaf_v1.0.16_3d0h` | 1.47 | 16.19 | 0.08 | 0.77 |

The host spends 89 % of each frame waiting for the GPU and 4 % importing. With
the harness's 171 ms/frame minus start-up that is one core spinning, as the
phase goal states. The host is never the bottleneck: the wait is the compute.

## Steady-state calibration

The harness fps cannot be compared across a change of the frame by itself, so
later plans use two runs of the same row, `--frames 600` and `--frames 20`
(`--repeat 3 --warmup 1` each), and
`steady fps = (600 - 20) / (rtime600 - rtime20)`.

| Run | rtime (3 runs, s) | Filter gpu ms | Spread |
| --- | --- | --- | --- |
| 600 frames | 25.72, 25.88, 25.56 | 22.34 to 22.40 | 1.2 % |
| 20 frames | 12.89, 12.91, 13.01 | 21.56 to 21.76 | 0.9 % |

Medians 25.72 s and 12.91 s give 22.09 ms per frame, **45.3 fps steady**, with
a start-up of 12.9 s at 20 frames. This agrees with the filter's own 21.8 to
22.4 ms. The 15 % target is therefore a frame of 18.8 ms or less (53 fps).

## Per-kernel breakdown (VTune xpu-offload)

One un-quoted VTune run (`--frames 100`, `SYCL_DEV_PROFILE_CAPS=1`, the explicit
PERFMON, SYS_PTRACE and SYS_ADMIN capabilities, no `--privileged`) gave the GPU
task times below, divided by 100 frames. Their sum is 19.87 ms per frame
against 21.8 ms of filter time (the rest is idle gaps and the wait). The run's
fps is not quoted.

| Task (instances per frame) | ms/frame | Share of 19.87 ms |
| --- | --- | --- |
| `IntegerVifHoriKernel<0>` (1; 3840x1600) | 6.33 | 31.8 % |
| `launch_vif_vert_impl<0>` (1) | 2.41 | 12.1 % |
| ADM `csf_den_cm`, scale 0 (1; plus 3 smaller) | 1.50 (2.33 in all) | 11.7 % |
| `IntegerVifHoriKernel<1>` (1; 1920x800) | 1.32 | 6.6 % |
| `motion` `submit_sad` (1) | 1.25 | 6.3 % |
| ADM `dwt_vert_pair` (4 scales) | 1.46 | 7.3 % |
| ADM `dwt_hori_pair` (4 scales) | 1.22 | 6.1 % |
| ADM `decouple_csf` (4 scales) | 1.28 | 6.4 % |
| chroma import + `detile_tile4` luma (2 each) | 0.69 + 0.42 | 5.6 % |
| `launch_vif_vert_impl<1..3>` + `IntegerVifHoriKernel<2,3>` | 0.69 | 3.5 % |
| `zeCommandListAppendMemoryCopy` and `Fill` | 0.21 | 1.1 % |

Group totals: VIF 11.0 ms (55 %), ADM 6.3 ms (32 %), motion 1.25 ms, import 1.1
ms, copies and fills 0.2 ms. The isolation estimate (VIF 12.1, ADM 6.0) is
within 10 % of this. About 37 launches run per frame, so the 2.2 ms between the
19.87 ms of GPU tasks and the 22.1 ms frame is about 60 microseconds per
launch. Scale 2 and 3 tasks of VIF and ADM are 1.2 ms in 14 launches. Host
side, `zeCommandQueueSynchronize` is 8.8 % of the 20 s run (the wait).
`IntegerVifHoriKernel<0>` alone is 32 % of the frame; it reads the seven 24.6 MB
planes (172 MB, 0.9 ms at 186 GB/s), so it runs at about 7 times its traffic
roof and is arithmetic-bound. The full task list is the evidence file
`13-02-vtune-tasks.csv`; `VMAF_SYCL_FORCE_READBACK` is no longer honoured (no
occurrence in `core/src`), so the readback ladder step was not run.

## Env rows (v0.6.1, R = 3)

Each row is compared with the branch scores; every one is bit-identical.

| Row | Harness fps | Filter gpu ms | Verdict |
| --- | --- | --- | --- |
| `VMAF_SYCL_DISPATCH=adm:graph,vif:graph,motion:graph` | 12.27 | 21.85 | no gain (graph, ADR-1121 stands) |
| `UR_L0_USE_IMMEDIATE_COMMANDLISTS=1` | 11.71 | 22.19 | slower (-5 %) |
| `NEOReadDebugKeys=1 OverrideEnableKmdNotify=1 OverrideKmdNotifyDelayMicroseconds=50` | 11.84 | 21.92 | slower; host CPU not lower (174 ms/frame) |
| `NEOReadDebugKeys=1 PowerSavingMode=1` | 11.88 | 21.88 | slower; host CPU not lower (174 ms/frame) |

The NEO debug keys do not reduce the spin on this i915 stack; they are not
user guidance.

## `n_subsample` rows (v0.6.1, R = 3)

| `n_subsample` | Run time of 200 input frames | Gain over 1 | Zero-copy vs CPU |
| --- | --- | --- | --- |
| 1 | 16.3 s | 0 | `IDENTICAL` |
| 2 | 14.9 s | 9 % | `DIFF` (motion), `IDENTICAL` after the fix below |
| 4 | 13.7 s | 16 % | `DIFF` (motion), `IDENTICAL` after the fix below |

The gain is small because imports and the temporal `motion` run on every
frame and the 12 s start-up is fixed; in steady state `n_subsample` 4 would
save about 4 ms of 22.

## Defect found

On the base and branch builds alike, `integer_motion2` / `integer_motion3` on
the zero-copy path at `n_subsample` 2 and 4 are about 55.7 on every frame where
the CPU gives 0.2 to 0.4, so `vmaf` reads 100 on frames where the CPU reads
85 to 89. The first scored frame is correct. Other features match. It is
recorded as `T-SYCL-ZEROCOPY-NSUBSAMPLE-MOTION-2026-10-06` in
[state.md](../state.md).

The import slots were not the cause: the host-upload path
(`vmaf --backend sycl --subsample 2`) fails the same way. The combined SYCL
queue enqueued a frame only after every registered extractor had submitted,
and on the frames `n_subsample` drops only the temporal `motion_sycl` submits,
so its kernels never ran on those frames. With the fix (libvmaf reports each
skipped extractor through `vmaf_sycl_graph_skip()`) the zero-copy runs of this
segment at `n_subsample` 2 and 4 score `IDENTICAL` to the CPU leg (100 and 50
scored frames), and `test_sycl_n_subsample_combined_graph` guards it.

## Bit-exact reference

The base zero-copy run of `vmaf_v0.6.1` and the CPU libvmaf leg on the same QSV
decode (hwdownload) score `IDENTICAL` over 200 frames, so the reference holds.

## Roofline and measured ceilings

A380 memory bandwidth 186 GB/s (Intel specification; 96-bit GDDR6). Frame is
22.1 ms; the compute engine idles 2.4 ms of it. Ceilings are upper bounds from
the measurements above, not predictions; "% fps" is the share of the 22.1 ms
frame.

| ID | Candidate | Measured ceiling | % fps | Bit risk | Reading |
| --- | --- | --- | --- | --- | --- |
| C2 | Low-power host wait | 0 ms; frees about one host core | 0 (may lose 1) | none | the wait is the compute; no fps to gain |
| C3 | Device fence on VA import, drop frame-start wait | up to 2.2 ms (the gap between 19.87 ms of tasks and the frame); host import is 0.78 ms | 3 to 10 | none if every reader is fenced | bounded by idle compute; blocked by the `n_subsample` defect |
| C1 | Skip chroma import for luma-only models | at most 0.3 ms (24 MB of traffic is 0.13 ms at 186 GB/s) | about 1 | none | inside the 2.5 ms import |
| K3 | De-tile 16 bytes per work-item | `detile_tile4` is 0.42 ms per frame (2 x 0.21); at most 0.2 saved | 1 | none | copy plus shift |
| K1 | VIF scale-0 (7 planes of 24.6 MB, written and read) | vert 2.41 ms and hori 6.33 ms are 8.7 ms; traffic roof 1.9 ms for the planes, so up to about 2.4 ms from the vertical pass, more only if the horizontal arithmetic shrinks | up to 11 | integer, needs `==` at 4K | `IntegerVifHoriKernel<0>` (32 % of the frame) is arithmetic-bound |
| K2 | ADM hotspot | 6.3 ms in 16 launches; the largest task `csf_den_cm` scale 0 is 1.5 ms; no task above 7 % of the frame | up to 7 | low | not selected |
| K4 | Dispatch or env default | 0 (graph neutral; immediate and NEO keys slower) | 0 | none | rejected by the env rows |
| K5 | Merge tiny scale-2/3 launches | 14 launches at about 60 microseconds of gap = about 0.8 ms; their own GPU time is 1.2 ms | up to 4 | none for integer | 37 launches per frame in all |

Excluded (research anti-candidates): per-surface import caching (ADR-1596 lost),
delayed frees, any float accumulation reorder, copy-engine imports,
`vif_fused` advice, NEO debug keys as a default.

## Consequences for the 15 % target

Only K1 and C3 reach more than 5 % in the ceiling column (K1 through its 8.7 ms of scale-0 tasks), and K5 and C3
share the same 2.2 ms idle gap, so their gains do not add. K1's ceiling needs
the whole intermediate traffic removed, and the `vif_fused` variant that does
so is not bit-exact. Realistic outcomes are well under half of the ceilings.
The measured irreducibility write-up (success criterion 2) is a likely
outcome and is cheap: it is this digest plus the user guidance.

## A/B results (plan 13-06)

Plan 13-06 measured every change of plans 13-03 to 13-05 on the same A380 and
segment, against the pre-phase library, and kept only the measured ones. Each
variant is a build snapshot: `base` (no phase-13 code), `wait` (C3, with the
`n_subsample` fix), `c1k3` (plus K3; C1 was not coded), `kern` (plus K1). The
runs were interleaved and rotated, six 600-frame and six 20-frame runs per
variant for `vmaf_v0.6.1`. "gpu ms" is the filter's own time per frame over 599
frames; "steady" uses the calibration formula above on the first, clean block
of three repeats. Host-side stalls in the second block (GPU idle at 0 MHz while
a run waited) made its rtime differences unusable; the gpu ms column did not
move with them (spread at most 0.4 % per variant).

| Variant | Steady ms/frame | Steady fps | gpu ms (6 runs) | Host CPU ms/frame |
| --- | --- | --- | --- | --- |
| `base` | 22.57 | 44.3 | 22.39 | 24.8 |
| `wait` (C3) | 22.53 | 44.4 | 22.38 | 24.6 |
| `c1k3` (+K3) | 22.69 | 44.1 | 22.33 | 24.8 |
| `kern` (+K1) | 21.37 | 46.8 | 21.17 | 23.5 |

VTune per frame (100 frames, xpu-offload): the scale-0 VIF horizontal pass went
from 6.32 ms (`IntegerVifHoriKernel<0,16>`) to 5.33 ms
(`IntegerVifHoriTiledKernel<0,16>`, SIMD16, no spill); `detile_tile4` from
0.208 to 0.182 ms per call, two calls per frame. The hardware occupancy and
local-memory counters (`gpu-hotspots`) need the Metrics Discovery library,
which the toolchain image does not have.

Verdicts (each with its numbers in the commit message of the revert):

- **K1 kept.** -1.16 ms of gpu time per frame and +6 % steady fps against
  `c1k3`; `vmaf_4k_v0.6.1` 22.38 to 21.16 ms. It is a third of the 13-05 ISA
  estimate: the tile load and the unchanged statistic and reduction remain.
  `vmaf_v1.0.16_3d0h` has no VIF feature and does not change (18.44 to 18.36 ms).
- **K3 reverted.** -0.05 ms per frame, inside the spread of the rows, and no
  steady-state gain. On the device its 16-byte stores also failed the new
  byte-for-byte test on a row that is not a multiple of 4 bytes (67-pixel
  NV12); the fix was reverted with it. The test stays and passes on the 4-byte
  kernel.
- **C3 reverted.** No fps gain and no host CPU drop. With `VMAF_SYCL_TIMING`
  the frame-start wait (`combined_wait`, 19.4 ms) is gone but the VA import now
  blocks for about 20.8 ms per frame: the libvmaf collect still waits for every
  frame, so the host still waits one frame per frame. Lowering the host CPU
  needs a different wait (C2, not selected), not a different place for it.
- **K5** was abandoned before coding (no independent launch worth more than
  about 0.1 ms per frame); C1, C2, K2 and K4 were not selected.

All variants are bit-identical to the pre-phase library on the 200-frame
segment for `vmaf_v0.6.1` and `vmaf_v1.0.16_3d0h`, and every variant after the
`n_subsample` fix matches the CPU at `n_subsample` 2 and 4, in graph replay
too. The SYCL device suite and the end-to-end harness (8 and 10 bit,
`--repeat 3`; batched `--repeat 10`) pass at the final head.

Result: at the final head (the `n_subsample` fix and K1, after the reverts)
three more interleaved pairs give 21.21 ms of filter time per frame against
22.36 ms for `base` (-5.1 %), and a steady state of 47.1 fps against 44.2 fps
(+6.5 %); against the 45.3 fps calibration above that is +4 %. The 15 % target
is not reached. The same tiled kernel compiles for VIF scales 1 to 3 (1.3 ms
and 0.4 ms of the frame) and is the next measured candidate.

## References

- [ADR-1769](../adr/1769-sycl-zerocopy-throughput-a380.md)
- [ADR-1121](../adr/1121-sycl-qsv-zerocopy-p010-normalization.md), [ADR-1369](../adr/1369-sycl-shared-planes-light-twins.md), [ADR-1596](../adr/1596-sycl-va-import-immediate-cmdlist.md), [ADR-1597](../adr/1597-sycl-zerocopy-planar-chroma-import.md)
- [Research-1395](1395-sycl-kernels-no-scratch.md), [Research-1595](1595-sycl-zerocopy-feature-correctness.md)
- [SYCL backend overview](../backends/sycl/overview.md)
