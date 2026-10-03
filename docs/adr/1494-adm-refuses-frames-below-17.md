<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1494: `adm` and `float_adm` refuse frames below 17x17; upstream's integer ADM crashes there and its float ADM returns values that at 8x8 and 12x9 depend on the heap

- **Status**: Accepted
- **Date**: 2026-10-03
- **Deciders**: lusoris
- **Tags**: `upstream-divergence`, `adm`, `memory-safety`, `correctness`, `rc3`, `fork-local`

## Context

The reference for code the fork inherited from Netflix/vmaf is Netflix's
source; a difference needs an ADR (see References). Both ADM extractors of
this tree refuse a frame with a dimension below 17 pixels at init: integer
`adm` since fork PR #1473, `float_adm` since fork PR #1770. Neither choice had
an ADR: [ADR-1482](1482-integer-adm-frames-17-to-32.md) names the integer
refusal and [ADR-1487](1487-upstream-parity-policy-and-guard.md) lists the
float one among the refusals it records in passing.

Both ADM pipelines decompose a frame into four wavelet levels, each halving
the band and rounding up. Below 17 pixels the scale-3 band has a single
sample. At Netflix `9e48141b`:

1. `float_adm`: `init()` (`libvmaf/src/feature/float_adm.c:316` to `:342`)
   accepts any size. At 8 pixels or less the scale-3 decomposition itself
   has a one-sample input, for which `dwt2_src_indices_filt_s()`
   (`libvmaf/src/feature/adm_tools.c:1021` onwards) builds the taps 1, 0, 0
   and -1: one past the input and one before it (an AddressSanitizer build
   reports a heap-buffer-overflow on an 8x8 pair, fork PR #1770). From 9 to
   16 pixels the decomposition stays inside its input, but the scores are
   computed on a one-sample scale-3 band.
2. Integer `adm`: `init()` (`libvmaf/src/feature/integer_adm.c:3116` to
   `:3186`) accepts any size and the extraction ends in a segmentation fault
   (reported as Netflix/vmaf#1607).

This tree calls `adm_frame_size_check()`
(`core/src/feature/adm_csf_fixed_point.h`, `ADM_MIN_FRAME_DIM` 17) from the
`init()` of both extractors and of their GPU twins (`float_adm_hip` and
`float_adm_metal` excepted, `T-GPU-FLOAT-ADM-TINY-FRAME-FLOOR-2026-10-01`). The
refusal is `-EINVAL` with the message
`<extractor> requires width >= 17 and height >= 17 (got WxH)`, and
[ADR-1481](1481-extractor-failure-fails-the-run.md) makes it reach the caller.

## Decision

The fork keeps both refusals. `adm` and `float_adm` on a frame with a
dimension below 17 pixels fail at init; upstream crashes (`adm`) or returns
a value (`float_adm`).

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Return upstream's `float_adm` values | Same output as upstream | At 8x8 and 12x9 the values change with the contents of the heap (`adm2` 1.42 or 3.6e-11 on the same 8x8 frame); at 16x16 they repeat, but describe a one-sample scale-3 band | A value read outside a buffer is not a reference, and at two of three sizes it is not reproducible |
| Score the small frame with a clamped index table | Returns a defined number | No upstream value to agree with, and a number for a decomposition that has no scale-3 content | The fork does not invent a score upstream cannot produce |
| Crash as upstream's integer ADM does | Same behaviour | A crash of the caller's process | Not a behaviour to keep |

## Consequences

- **Measured size** (upstream parity guard, full matrix in the dev container
  image `sha256:43ef1e32cb32`, GCC 15.2.0, glibc 2.43, x86-64, 2026-10-03;
  Netflix `9e48141b` against this tree; the 16x16, 12x9 and 8x8 crops of the
  Netflix 576x324 pair, two frames, scalar, AVX2 and default dispatch):
  - Integer `adm`: upstream ends in signal 11 on all three sizes, at every
    dispatch and with either heap fill; this tree returns `-EINVAL`
    (fragment `adm.frames-of-16-or-less`, 18 runs).
  - `float_adm`: upstream returns values, this tree `-EINVAL` (fragment
    `float_adm.frames-of-16-or-less`, 18 runs). Upstream at the scalar
    dispatch, first frame: 16x16 `adm2` 1.3704558438779075, `adm_scale3`
    3.4950337187093137, the same with the heap filled by
    `MALLOC_PERTURB_=170`; 12x9 `adm2` 3.9378338827804091, with the heap
    filled 3.3793206694421678; 8x8 `adm2` 1.4166728196691425, with the heap
    filled 3.611113250237721e-11. The guard's heap check finds 294 upstream
    outputs of `float_adm` on the 12x9 and 8x8 crops that change with the
    heap's contents.
  No frame of 17 pixels or more is affected.
- **Upstream status**: Netflix/vmaf#1642, sent by the fork, makes upstream's
  integer ADM refuse such frames (open; the report is Netflix/vmaf#1607). No
  upstream change exists for `float_adm`.
- **Ends when** upstream refuses these frames in both extractors. With
  #1642 merged and a `float_adm` counterpart, upstream's status becomes this
  tree's (an error at init) and the two `error` fragments of the upstream
  parity guard go stale and are removed.
- **Neutral**: `float_adm_hip` and `float_adm_metal` still accept such
  frames (`T-GPU-FLOAT-ADM-TINY-FRAME-FLOOR-2026-10-01`); the guard compares
  the CPU only.

## References

- Fork PR #1473 (integer `adm`), fork PR #1770 (`953cf6ea6`, `float_adm`);
  [ADR-1481](1481-extractor-failure-fails-the-run.md),
  [ADR-1482](1482-integer-adm-frames-17-to-32.md),
  [ADR-1487](1487-upstream-parity-policy-and-guard.md).
- Upstream: `libvmaf/src/feature/float_adm.c:316` to `:342`,
  `libvmaf/src/feature/adm_tools.c:1021`,
  `libvmaf/src/feature/integer_adm.c:3116` to `:3186` at Netflix `9e48141b`;
  Netflix/vmaf#1642, #1607.
- Source: `req` (popup answer, 2026-10-02): "Netflix's source, deviations
  only by ADR".
