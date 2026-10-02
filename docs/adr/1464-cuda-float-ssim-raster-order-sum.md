<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1464: `float_ssim_cuda` adds its frame sums in the CPU's raster order, on the host

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `cuda`, `gpu-parity`, `numerics`, `ssim`, `testing`, `rc3`, `fork-local`

## Context

`float_ssim_cuda` is declared an exact twin of the CPU `float_ssim`
(`scripts/ci/exact_twins.d/float_ssim.cuda`, `float_ssim_lcs.cuda`,
[ADR-1457](1457-cuda-exact-twins-declared.md)). Its decimated planes, both
Gaussian passes and the per-window `l * c * s` are the CPU's bit for bit
([ADR-1399](1399-cuda-float-ssim-device-decimation.md)). One thing was not:
`iqa/ssim_tools.c::iqa_ssim()` adds every window's term into one `double`
per sum, left to right and top to bottom, and returns
`(float)(sum / windows)`, while the twin added the same terms per warp, per
16x8 block and then on the host. ADR-1457 recorded that as "exact up to one
rounding" and stated the rule for the case that a mean ever differs: the twin
adds in raster order, and the listing does not get a tolerance.

A mean differs. A search over uniform noise at 64x64 found two frames in
3.1e7 whose `float_ssim` is one float step from the CPU's
(`T-GPU-FLOAT-SSIM-FRAME-SUM-ORDER-2026-10-02`). On the frame pair kept in
`core/test/float_ssim_order_frame.h` the CPU returns
-4.222829943500983e-07 (`0xb4e2b622`) and the twin returned `0xb4e2b621`. A
sum of doubles is its order: the two sums differ by the rounding of the
sequential adds, and here the mean lies that close to a float rounding
boundary. The HIP and SYCL twins returned the CUDA twin's value.

## Decision

`float_ssim_cuda` forms every frame sum as the CPU does, in the form
[ADR-1424](1424-cuda-ssim-cpu-frame-sum.md) gave `integer_ssim_cuda`:

- The pass-2 kernels reduce nothing. `calculate_ssim_vert_combine` stores each
  window's `l * c * s` at the window's raster index;
  `calculate_ssim_vert_combine_lcs` stores four doubles per window, the same
  value followed by its `l`, `c` and `s`.
- The host reads the plane back. `float_ssim_frame_sum()` adds the terms in
  index order; under `enable_lcs`, `float_ssim_frame_sums_lcs()` adds the four
  sums in one pass, each in index order. `iqa_ssim()` keeps one accumulator
  per sum, so the four are independent of one another.
- The mean and its `float` rounding are unchanged
  (`float_ssim_frame_mean()`).

This holds at every scale and frame size. The fragments stay; this ADR is the
reason they are true.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Terms read back, host adds in raster order (this ADR) | The CPU's sum by construction; the precedent's form, 60 lines; nothing to prove | 8 bytes per window read back (32 with `enable_lcs`) and one host add per window and sum: about 1 ns per window, 3.3 ns with `enable_lcs` | Chosen: results first |
| Remove the fragments and bound the cells at one float step | No cost | The twin is then not the CPU's `float_ssim`; ADR-1457 ruled this out in advance | Maintainer decision: no bound |
| Make the CPU's sum independent of the order (exact or compensated) | Every twin stays as it is | Changes CPU scores on such frames and has to hold the Netflix golden gate | Maintainer decision: no CPU change |
| `core/src/feature/ordered_sum.h` on the device ([ADR-1433](1433-cuda-ssimulacra2-cpu-sum-order.md)) | The sequential sum's bits without a read-back | Its integer form needs non-negative terms. `l` and `c` qualify; `s` and the product `l * c * s` are negative where the covariance is, and the frame of this ADR sums to -1.2e-3 | Does not cover the sum that differed |
| Keep the block sum and prove the rounded mean from it: the sequential sum lies within `2 (n - 1) 2^-53` times the sum of the terms' magnitudes of any other order of the same terms, so when both ends of that interval round to one float the mean is known, and only otherwise the terms are read back | The read-back and the host adds happen on 3 to 6 frames in a hundred at 8 million windows and on fewer than one in a thousand at 122 200 | A second code path and an error bound that has to be argued and tested; not the precedent's form | Recorded as the tuning candidate in `T-CUDA-FLOAT-SSIM-EXACT-THROUGHPUT-2026-10-02` |
| One device thread adds the plane | No read-back | Millions of dependent double adds on one device thread; ADR-1400 measured 2.2 µs a pixel on a gfx1036 | Slower than the host by orders of magnitude |

## Consequences

- **Positive**: on the frame of `float_ssim_order_frame.h` the twin returns
  `0xb4e2b622`, the CPU's bits (`test_cuda_float_ssim_order`, which fails on
  the earlier twin with `cpu=0xb4e2b622 cuda=0xb4e2b621`).
- **Positive**: measured on an RTX 4090 at `--precision max` against
  `--backend cpu` of the same build, 9828 of 9828 values identical:
  `float_ssim` alone and with `enable_lcs`, `scale=1`, `scale=2`,
  `scale=3` with `enable_lcs`, `enable_db`, `clip_db`, on the typical set
  (Netflix 576x324 at 8 and 10 bits, both 1080p checkerboards, 200 frames of
  BBB 3840x2160; 6405 values), the stress set (Netflix at 12 and 16 bits and
  as 10-bit 4:2:2, Sparks, noise at four bit depths, a bright 16-bit 1080p
  pair, 16-bit BBB at 1080p and 4K; 3045 values) and frames of 40x40, 56x56
  and 64x64 (378 values). 220 000 noise frames at 64x64 with `enable_lcs`,
  the chunk that held the differing frame among them: 880 000 of 880 000.
  The gate cells `float_ssim` and `float_ssim_lcs` report 0 at tolerance 0 on
  the Netflix pair and on 200 BBB frames.
- **Negative**: time, in proportion to the windows scored. Per frame through
  the `vmaf` tool, medians of 11 to 15 interleaved pairs, host load average
  60 to 80:

  | Input and request | Windows | Before | After | Paired difference |
  |---|---:|---:|---:|---:|
  | 576x324 (automatic scale 1) | 177 724 | 0.14 ms | 0.33 ms | +0.19 ms |
  | 576x324, `enable_lcs` | 177 724 | 0.15 ms | 0.75 ms | +0.59 ms |
  | 1920x1080 (automatic scale 4) | 122 200 | 1.12 ms | 1.22 ms | -0.14 ms |
  | 1920x1080, `enable_lcs` | 122 200 | 0.84 ms | 1.02 ms | +0.14 ms |
  | 3840x2160 (automatic scale 8) | 122 200 | 3.53 ms | 3.69 ms | +0.35 ms |
  | 3840x2160, `enable_lcs` | 122 200 | 4.72 ms | 4.94 ms | -1.02 ms |
  | 1920x1080, `scale=1` | 2 043 700 | 0.88 ms | 3.10 ms | +2.15 ms |
  | 1920x1080, `scale=1`, `enable_lcs` | 2 043 700 | 1.17 ms | 8.04 ms | +7.06 ms |
  | 3840x2160, `scale=1` | 8 234 500 | 3.91 ms | 12.55 ms | +8.58 ms |
  | 3840x2160, `scale=1`, `enable_lcs` | 8 234 500 | 4.19 ms | 30.92 ms | +26.80 ms |

  At the automatic scale of 1080p and 4K input the difference is inside the
  noise. Where the picture is scored undecimated, which is the automatic
  scale below a 384-pixel short side and an explicit `scale=1` above, the
  twin takes 2.4 to 3.5 times as long, and 5 to 7 times with `enable_lcs`.
  Where the +9.1 ms of a 3840x2160 `scale=1` frame go, from builds with a
  stage removed (9 pairs each): the kernel storing the plane instead of
  reducing it, nothing measurable; the read-back of its 66 MB, 5.0 ms; the
  host's 8.2 million adds, 4.2 ms. `T-CUDA-FLOAT-SSIM-EXACT-THROUGHPUT-2026-10-02`.
- **Negative**: memory. The term plane is 8 bytes per window on the device
  and as pinned host memory, 32 with `enable_lcs`: 1 MB (4 MB) at the
  automatic scale of 1080p and 4K, 66 MB (263 MB) for a 3840x2160 frame at
  `scale=1`. The per-block partials it replaces were a few kilobytes.
- **Negative**: a stored `float_ssim_cuda` score can change by one float
  step on a frame whose mean lies within the sum's rounding error of a float
  boundary; none of the measured content frames does.
- **Neutral / follow-ups**:
  - `float_ssim_hip` and `float_ssim_sycl` have the same defect and are
    fixed by their own lanes; the frame header is shared so the three tests
    score the same bytes.
  - `float_ms_ssim_cuda` adds the `l`, `c` and `s` terms of each scale per
    block as well. Four frames in 8.3 million noise frames at 176x176 have
    a per-scale mean one float step from the CPU's; its fix is a separate
    change on `fix/cuda-float-ms-ssim-raster-order-sum`, which records them.
  - The contract mirrors `iqa_ssim()` and its accumulators. A change to the
    term or to the order of the sums changes the kernels and
    `float_ssim_frame_sum()` / `float_ssim_frame_sums_lcs()` in the same PR.
    `test_cuda_float_ssim_exact_contract.py` pins the design without a
    device.

## References

- `req` (coordinator brief, 2026-10-02, `w21-float-ssim-raster.md`): "The
  repository's own rule (`core/src/feature/cuda/AGENTS.md`, from #1814: "Mean
  ever differs -> add terms in raster order as `integer_ssim_cuda`") and the
  maintainer's choices this week (exact now, speed in RC7) settle it: the twin
  adds the CPU's terms in the CPU's order. Not a bound, not a CPU change. The
  fragments stay; your PR makes them true."
- [ADR-1424](1424-cuda-ssim-cpu-frame-sum.md),
  [ADR-1457](1457-cuda-exact-twins-declared.md),
  [ADR-1399](1399-cuda-float-ssim-device-decimation.md),
  [ADR-1373](1373-cuda-twin-cpu-option-parity.md),
  [ADR-1433](1433-cuda-ssimulacra2-cpu-sum-order.md),
  [ADR-1428](1428-exact-twins-fragments.md).
