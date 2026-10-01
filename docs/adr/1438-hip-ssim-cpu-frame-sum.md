<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1438: `integer_ssim_hip` adds its terms in the CPU's raster order at every frame size and returns the CPU's score bit for bit

- **Status**: Accepted
- **Date**: 2026-10-01
- **Deciders**: lusoris
- **Tags**: `hip`, `ssim`, `gpu-parity`, `numerics`, `testing`, `ci`, `rc3`, `fork-local`

Supersedes [ADR-1400](1400-hip-integer-ssim-raster-sum-small-frames.md).

## Context

`integer_ssim_hip` is the HIP twin of the fixed-point `ssim` extractor
(`integer_ssim.c`). Its moments are int64 and equal the CPU's, and its
per-pixel term is the CPU's double expression operand for operand, built
without FMA contraction ([ADR-1407](1407-hip-strict-fp-every-kernel.md)).
What is left is the frame sum: `calc_ssim()` adds every term into one
`double`, left to right and top to bottom.

[ADR-1400](1400-hip-integer-ssim-raster-sum-small-frames.md) made the twin
add in that order for frames of at most 4096 pixels, where the order decides
between a finite dB value and `+inf` on identical frames, and kept a
per-block tree above that size because a full-frame read-back is not free.
Measured on a gfx1036 at `--precision max` against the CPU on `origin/master`
80c5a0332 (the RC3 sweep of the HIP twins, #1772): the score of no frame
above the bound was the CPU's. 1 of 178 frames
identical; the others up to 1.1e-11 away (2.3e-14 on the Netflix 576x324
pair, 1.1e-11 on the 10 px checkerboard, 5.6e-13 at 3840x2160).

A sum of doubles is its order. No reduction over blocks or waves rounds like
the sequential one, so the twin either adds in the CPU's order or keeps a
tolerance. [ADR-1424](1424-cuda-ssim-cpu-frame-sum.md) settled that for the
CUDA twin: store the terms, add them on the host. RC3 asks the same of every
twin ([ADR-1421](1421-rc3-rc8-candidate-map.md)).

## Decision

We will lift ADR-1400's bound: `integer_ssim_hip` has one pass-2 kernel,
`integer_ssim_vert_terms`, which stores every pixel's term at its raster
position and reduces only the integer window weights per block; the host
reads the term plane back and adds it in index order, which is
`calc_ssim()`'s order, at every frame size. The per-block term tree
(`integer_ssim_vert_combine`), its identical-window rule
(`issim_pixel_term()`) and `ISSIM_HIP_RASTER_MAX_PIXELS` are removed. `ssim`:
`hip` is declared an exact twin, so the parity gate compares the cell with
tolerance 0 at `--precision max`.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Terms per pixel, host adds in raster order (this ADR) | The CPU's double at every size; one pass-2 kernel instead of two and no size switch | 8 bytes per pixel of device and pinned host memory (66 MB at 3840x2160) and a sequential host sum: 6 % more time per 1920x1080 frame, 4 % per 3840x2160 frame on the gfx1036 | Chosen |
| Keep ADR-1400's bound; tighten the tolerance to 1.1e-11 | No read-back above 4096 pixels | Not the CPU's score; 30 times more in dB, and a tolerance is a number someone measured, not a property | RC3 asks for the bits |
| Terms and weights per pixel (the ADR-1400 path at every size) | No kernel change at all: only the bound moves | Twice the read-back (133 MB at 3840x2160) for a sum that does not depend on the order | The weights are integers; a block reduction is exact |
| Compute the weight total on the host from the frame size | No weight read-back, no reduction in the kernel | The 9 taps would exist a second time on the host | The block sum costs 8 bytes per 128 pixels |
| One device thread adds the plane in raster order | No read-back | ADR-1400 measured 2.2 µs a pixel on this device: 18 s per 3840x2160 frame | Slower than the host by three orders of magnitude |
| Per-row device sums, rows added on the host | Small read-back | `calc_ssim()` has no per-row accumulator; a row's contribution depends on the sum before it | Not the CPU's order |

## Consequences

- **Positive**: measured on a gfx1036 at `--precision max`, the score of
  every frame equals `--backend cpu`: 178 of 178 (1 before) on the Netflix
  576x324 pair at 8, 10, 12 and 16 bits and as 10-bit 4:2:2, both 1920x1080
  checkerboard pairs, Sparks 480x270 at 10 bits, 48 frames of BBB 3840x2160,
  full-range noise at four depths and a bright 16-bit 1080p pair. The same
  with `enable_db` and with `enable_db` plus `clip_db` (178 of 178 each).
- **Positive**: an identical frame reports the CPU's value, finite or
  `+inf`, at every size. Above 4096 pixels the twin used to force an
  identical window to its weight, which is what the CPU reports on all but a
  few in a million identical frames of that size; now it is what the CPU
  reports on all of them.
- **Positive**: one pass-2 kernel and no frame-size switch in the host.
- **Negative**: a frame takes 28.2 ms before and 30.0 ms after at 1920x1080
  (+6 %), 94.3 and 98.1 ms at 3840x2160 (+4 %): medians of 21 interleaved
  pairs of runs on the gfx1036 under other lanes' load. The frame's terms
  are read back (16.6 MB and 66 MB) and added one after the other on the
  host.
- **Negative**: 66 MB more device memory and as much pinned host memory at
  3840x2160, next to the six int64 moment planes (398 MB) the twin holds.
- **Negative**: stored `integer_ssim_hip` scores change by up to 1.1e-11.
- **Neutral / follow-ups**: `integer_ssim_sycl` and `integer_ssim_metal`
  still reduce per block (`T-GPU-SSIM-FRAME-SUM-ORDER-2026-10-01` stays open
  for them). Guards: `test_hip_ssim_parity` in its four registrations (`==`
  on eight frames each; every one fails on the old twin),
  `test_hip_ssim_tiny_frames` (`==` in dB from 1x1 to 322x182 at four bit
  depths; 65x64 fails on the old twin) and six planted regressions in
  `test_hip_kernel_source_contract.py`.

## References

- `req` (maintainer brief for the second HIP lane, 2026-10-01): "Not identical: find the cause (order of additions, contraction, float vs double, rounding shift, border, libm), fix it the way the CUDA / SYCL twin of the same feature was fixed if one was (read that ADR and reuse its helpers [...])"
- [ADR-1424](1424-cuda-ssim-cpu-frame-sum.md) (the CUDA twin),
  [ADR-1400](1400-hip-integer-ssim-raster-sum-small-frames.md) (superseded),
  [ADR-1397](1397-psnr-hvs-twins-cpu-float-sum.md) (the exact cell),
  [ADR-0564](0564-integer-ssim-gpu-real-kernels.md),
  [ADR-0214](0214-gpu-parity-ci-gate.md).
- `docs/state.md`: `T-GPU-SSIM-FRAME-SUM-ORDER-2026-10-01` (HIP part).
