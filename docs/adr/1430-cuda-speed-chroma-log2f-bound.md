<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1430: `speed_chroma_cuda` keeps its correctly rounded `log2`; the gate bounds what glibc's `log2f` adds

- **Status**: Accepted
- **Date**: 2026-10-01
- **Deciders**: lusoris
- **Tags**: `cuda`, `gpu-parity`, `numerics`, `speed`, `testing`, `ci`, `rc3`, `fork-local`

## Context

`speed_chroma_cuda` reproduces `speed.c` operation for operation since
[ADR-1380](1380-cuda-speed-device-resident-pipeline.md), and rounds `log2`
correctly on the device (fp32 pairs plus a table of 48 hard cases,
`speed_log2()` in `core/src/feature/cuda/speed/speed_score.cu`). `speed.c`
calls the C library's `log2f`. Against a CPU build whose `log2f` rounds
correctly, such as an icx build with Intel's `libimf`, the twin is
bit-identical ([SpEED](../metrics/speed_qa.md#the-cpu-reference-and-log2f)).

Against a GCC build on glibc it is not, on a few outputs. The exactness pass
of this lane measured which and why
([Research-1430](../research/1430-cuda-speed-chroma-log2f-bound.md)):

- RTX 4090, glibc 2.44, `--precision max`, the Netflix 576x324 pair at 8, 10,
  12 and 16 bits, both 1080p checkerboard pairs and 200 frames of BBB
  3840x2160: 776 of 789 outputs are identical. The 13 others differ by
  4.8e-7 to 1.4e-6, one to five steps of the fp32 score.
- The same CPU binary run with a correctly rounded `log2f` preloaded
  (`LD_PRELOAD`, `(float)log2((double)x)`) returns the twin's bits on all 789
  outputs. The CPU run changes on exactly the 13 outputs, by exactly the
  twin's difference. Nothing else separates the two sides.
- glibc's `log2f` returns the neighbouring float for 0.015 % to 0.97 % of the
  arguments of a binade (four whole binades enumerated), never further.

The cell had no entry in the cross-backend gate, and the C parity test
allowed `1e-4`. The maintainer's direction for this lane is that a twin
reproduces the CPU bit for bit, and that where the CPU's own math library is
the obstacle, the affected outputs and frames are named and everything else
is identical.

## Decision

The twin does not change. Its `log2` stays correctly rounded.

The gate gets the cell: feature `speed_chroma` with the three scores
`speed_chroma_u`, `speed_chroma_v` and `speed_chroma_uv`, and
`LIBM_TWINS["speed_chroma"] = {"cuda": 5e-6}`
([ADR-1426](1426-cuda-ciede-cpu-arithmetic.md) introduced the table for
`ciede`). The cell runs at `--precision max`. The bound is derived from what
the difference is made of: the scores are fp32 values, so a difference is a
whole number of float steps; the largest count measured is five; the gate's
fixtures score 3 to 11, and below 16 a step is at most 2^-20. Five such steps
are 4.77e-6, written as `5e-6`. The count does not depend on the score, the
step does, so a fixture scoring above 16 needs the bound scaled.

`test_cuda_speed_chroma_parity` compares all three scores of every frame to
one part in a million of the CPU's score, instead of one score of one frame
at `1e-4`, and on a 960x960 textured fixture whose covariance is regular. Its
768x432 ramp gave a covariance that is singular on every frame, so the
scoring path with its `log2f` calls never ran. The new fixture scores about
22 and is 3.8e-6 (two steps, 1.7e-7 of the score) from the CPU on one frame,
and identical with the correctly rounded `log2f` preloaded; that is why the
test's bound is relative.

`T-CUDA-SPEED-CHROMA-GLIBC-LOG2F-2026-10-01` stays open with the list of
outputs and frames, because the cell is not `0`.

## Alternatives considered

| Option | Result | Verdict |
|---|---|---|
| Evaluate glibc's `log2f` algorithm on the device | Would return glibc's float where that algorithm is the one installed. glibc carries two `log2f` versions today (`log2f@GLIBC_2.2.5` and `log2f@@GLIBC_2.27` in `libm.so.6`), and an icx build links Intel's `libimf` instead, which rounds correctly. The twin would match one host library and stop matching the others, the icx build of this repository included | Rejected: it trades a bound that holds everywhere for an identity that holds on one libm |
| Give `speed.c` a `log2f` with defined rounding (the route `ssimulacra2_math.h` took for `cbrtf` and the sRGB EOTF) | Makes the CPU reference the same on every host and the twin bit-identical to it. Changes CPU `speed_chroma` and `speed_temporal` outputs by up to 1.4e-6 on glibc hosts, in an extractor ported from upstream | Not here: it is a change of the CPU reference, outside a twin lane, and `T-ICX-LIBIMF-HOST-MATH-2026-10-01` already tracks the host-library dependence for four features |
| List the cell in `EXACT_TWINS` and run the gate only against icx builds | CUDA builds are GCC builds here; icx is the SYCL toolchain | Rejected: the listed cell would fail on the build it is run against |
| Leave the cell out of the gate (status quo) | A regression of up to `1e-4` in the twin would pass its only test | Rejected |
| **A bound in float steps of the score, with the cause on record** | `5e-6` (five steps below 16) instead of an unlisted cell and `1e-4` | **Chosen** |

## Consequences

- **Positive**: the CUDA `speed_chroma` cell is gated, at a bound 10 times
  tighter than the places=4 default, and its test reaches the scoring path
  for the first time. The remaining difference is attributed completely, to
  a function on the CPU side.
- **Negative**: none in the product; no source of the twin changes and no
  score changes. The cell is not exact and the row stays open.
- **Neutral / follow-ups**:
  - The absolute bound does not fit content whose scores exceed 16; such a
    fixture needs the tolerance scaled with the float step of its scores.
  - The 13 outputs are a property of the host's `log2f`, not of the device.
    Another glibc version has another set of the same size (2.43: 6 of 48
    Netflix `speed_chroma_u` frames, at most 4.8e-7, recorded in
    `docs/metrics/speed_qa.md`).
  - `speed_temporal_cuda` is identical on the same fixtures (113 of 113
    values, BBB cut to 50 frames) and is not part of this decision; it has no
    gate cell yet.
  - A CPU `log2f` with defined rounding would let the cell move to the exact
    table. That is the second alternative above and belongs to
    `T-ICX-LIBIMF-HOST-MATH-2026-10-01`.

## References

- `req` (maintainer brief, 2026-10-01): "results before speed; a twin
  reproduces the CPU bit for bit, and tuning comes afterwards."
- `req` (maintainer brief, 2026-10-01): "Where the CPU's own libm call is the
  obstacle (glibc log2f vs correctly rounded, ADR-1380), say exactly which
  outputs and frames, and get everything else identical."
- [ADR-1380](1380-cuda-speed-device-resident-pipeline.md),
  [ADR-1426](1426-cuda-ciede-cpu-arithmetic.md),
  [ADR-0214](0214-gpu-parity-ci-gate.md),
  [ADR-0965](0965-cuda-speed-tu-repair.md).
- [Research-1430](../research/1430-cuda-speed-chroma-log2f-bound.md).
