<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1434: `float_adm_sycl` computes the CPU's arithmetic without an fp64 type, adds in the CPU's order and returns the CPU's scores bit for bit

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `sycl`, `gpu-parity`, `numerics`, `float-adm`, `testing`, `ci`, `rc3`, `fork-local`

## Context

With its scratch memory gone ([ADR-1395](1395-sycl-kernels-no-scratch.md)),
`float_adm_sycl` was up to 1.7e-5 from the CPU extractor on an Arc A380 at
`--precision max` (BBB 3840x2160, `adm_scale3`) and matched it on 59 of 336
outputs of the Netflix 576x324 pair
(`T-SYCL-FLOAT-ADM-NOT-CPU-ARITHMETIC-2026-10-01`). The picture conversion,
both DWT passes and the decouple's quotient are the CPU's fp32 operations in
the CPU's order: since [ADR-1442](1442-float-adm-reference-divides.md) the
reference divides, as the twin did, where upstream multiplies by a reciprocal
refined from the processor's `RCPSS` estimate. Everything else after the DWT
was not the CPU's:

1. **Enhancement gain.** `MIN(rst * adm_enhn_gain_limit, t)` multiplies and
   compares in `double`. The twin multiplied in fp32.
2. **CSF constants.** `FLOAT_ONE_BY_30` and `FLOAT_ONE_BY_15` are `double`
   literals: the filtered value is a `double` product rounded once, and the
   centre tap of `adm_cm_thresh3x3_s()` is added to the fp32 sum in `double`.
   The twin used fp32 constants and fp32 adds.
3. **Angle threshold.** The reference compares with
   `(cos^2 * |o|^2) * |t|^2`; the twin with `cos^2 * (|o|^2 * |t|^2)`, and
   with its own fp32 literal for `cos^2`.
4. **Threshold order.** The reference adds nine taps per band with the centre
   fifth, then the three band sums. The twin added 24 neighbours, then three
   centres.
5. **Reduction order.** `adm_csf_den_scale_s()` and `adm_cm_s()` add each row
   left to right into one fp32 accumulator per band and the rows into
   another. The twin summed per work-item, per sub-group and then in `double`
   on the host.
6. **Host tail.** The twin had its own copy of the CSF weights
   (`dwt_quant_step()`), its own pooling roots and a frame floor of `1e-2`
   where `compute_adm()` uses `1e-10`
   (`T-GPU-FLOAT-ADM-FRAME-SUM-FLOOR-2026-10-01`).

[ADR-1420](1420-cuda-float-adm-cpu-arithmetic.md) removed the same
differences from the CUDA twin and exported what a twin needs from the
reference: `adm_float_reference.h` (weights, region, pooling, angle
constant). A CUDA kernel has `double`. A SYCL kernel has not
([ADR-0220](0220-sycl-fp64-fallback.md): one fp64 instruction blocks the
translation unit on Arc A-series), so causes 1 and 2 cannot be fixed by
writing the reference's types.

The direction for the GPU twins is that a twin returns the CPU's bits; speed
comes afterwards.

## Decision

We will run the reference's arithmetic operation for operation in the SYCL
kernels, evaluate its three fp64 expressions without the fp64 type, add in
its order, and conclude on the host with its own routines.

**The reference's operations** live in
`core/src/feature/sycl/sycl_float_adm_math.h`, function for function with
the CUDA twin's `float_adm_device.h`: `divs()` (the fp32 quotient, which the
device rounds correctly under the flag line of
[ADR-1367](1367-sycl-strict-fp-every-feature-tu.md)), `angle_flag()`,
`decouple_band()`, `csf_flt()`, `thresh_band()`, `den_term()`, `cm_term()`,
`row_sum()`. The header also holds what one work-item of each kernel does
(`decouple_sample()`, `terms_sample()`, `row_item()`), so the extractor and
the test's probe run the same code.

**The three fp64 expressions** are

```c
rst * adm_enhn_gain_limit            /* compared with t, then rounded */
FLOAT_ONE_BY_30 * fabsf(csf)         /* rounded to float */
sum + FLOAT_ONE_BY_15 * fabsf(c)     /* float sum, double addend */
```

- *Gain.* When the limit is an fp32 value (the default 100 and the models' 1
  are), the fp64 product of two fp32 values is exact, so its rounding is the
  fp32 product's and the comparison of the rounded product with `t` selects
  what the exact comparison selects. Any other limit replays the fp64 product
  and comparison in 64-bit integers.
- *The two products with a constant.* Each constant is carried as an fp32
  pair (`hi + lo`, good to 2^-48) and as its exact 53-bit significand. The
  kernel forms `constant * a`, and `sum + constant * a`, as an exact fp32
  pair (`two_prod`, `ff_add` of `sycl_exact_fp.h`). The pair is good to about
  2^-24 of an fp32 step. When it lies within 2^-18 of a step of a point where
  the rounding to fp32 changes, or an operand is below 2^-100, the kernel
  replays the reference's fp64 multiplication and addition on `SoftDouble`
  values (`sycl_soft_double.h`, [ADR-1432](1432-sycl-integer-vif-exact-gain.md)),
  each rounded to nearest even as the fp64 operation it stands for, and rounds
  the result once. `sycl_soft_double.h` gains conversions for subnormal
  inputs and results. By the width of the zone the replay takes one
  evaluation in 131 072 when the low bits of the products are spread evenly.

**Order.** The decouple kernel writes the four CSF buffers. A term kernel
writes, for every sample of the reduced region, the nine terms the three
reductions accumulate (denominator, adm2 numerator, AIM numerator, three
bands each), with the masking threshold as one sum per band and the centre
tap fifth. A row kernel adds each row of each slot left to right in one fp32
accumulator, one work-item per row at sub-group size 8, as
[ADR-1411](1411-sycl-float-motion-cpu-float-sum.md) does for `float_motion`.
The host adds the rows top to bottom in fp32. One device-to-host copy per
frame carries the rows of all four scales.

**The reference's own routines.** `adm_csf_rfactor_s()` gives the weights,
`adm_border_s()` the region, `adm_pool_bands_s()` each scale's value,
`adm_decouple_cos_1deg_sq_s()` the angle constant; the frame floor is
`compute_adm()`'s expression. The twin's copies are deleted.

**Options.** Because the weights and the scale loop are now the reference's,
the twin takes the options the CPU has and it lacked: `adm_f1s0..3`,
`adm_f2s0..3`, `adm_skip_aim_scale` and `adm_skip_scale0`. `adm_p_norm = 1`
is exact (the terms are the samples, as `powf(x, 1)` is). `adm_csf_mode`
other than 0 stays rejected.

**No scratch memory.** Band values are named scalars and every helper takes
its band as a constant; the CSF weights are three named fields
([ADR-1395](1395-sycl-kernels-no-scratch.md)).

**Gate.** `float_adm` is declared exact for `sycl` by the fragment file
`scripts/ci/exact_twins.d/float_adm.sycl` (the form ADR-1428, PR #1745,
introduces).

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Exact fp32 pairs with a zone, integer replay inside it (this ADR) | The reference's value by construction; fp64-free; scratch-free; 12.3 ms per 4K frame, 15.1 before | Two evaluation paths per expression and a margin to justify | Chosen |
| The integer replay for every sample | One path | A 106-bit product and a 64-bit add for six values per sample of every scale | The pair decides all but one evaluation in 131 072 |
| fp32 constants and an fp32 gain, keep the 5e-5 tolerance | No change | 1.7e-5 from the CPU on a feature of float VMAF models; `adm2 = 1` where the CPU reports 0 on near-flat content without the noise floor | The direction is bit for bit |
| Emulated fp64 from the device compiler | The reference's source text | Arc A-series reject a kernel with an fp64 instruction (ADR-0220) | Not available |
| Finish the fp64 expressions on the host | Real `double` | They sit in the middle of the per-sample chain: the CSF buffers feed the 3x3 threshold | Would move the whole stage to the host |
| Make the CPU use fp32 constants and an fp32 gain | Twins need no replay | Changes scores the Netflix golden assertions pin; ADR-1442 changed the division only because the golden gate held | [ADR-0024](0024-netflix-golden-preserved.md) |
| Keep the per-sub-group reduction | No term buffer (48 MB at 3840x2160) | Another order than the reference's rows: not its sums | Order is part of the result |

## Consequences

- **Positive**: measured on an Arc A380 (xe, Level Zero, icpx 2026.0) at
  `--precision max` against the CPU extractor of the same build, every output
  of every frame is identical: the Netflix 576x324 pair at 8 bits (48
  frames), both 1920x1080 checkerboard pairs (3 frames each) and BBB
  3840x2160 (200 frames) through the parity gate; against a GCC build of the
  CPU extractor, with `debug=true` (18 outputs), also at 10, 12 and 16 bits
  and as 4:2:2 10-bit (3 frames each), with two exceptions that are the
  host's `powf`, not the twin (below). Options identical on the Netflix pair
  and a checkerboard pair: `adm_enhn_gain_limit` 1.0, 1.2 and 37.5,
  `adm_bypass_cm`, `adm_skip_scale0`, `adm_skip_aim_scale`, `adm_f1s0` /
  `adm_f2s2`, `adm_adm3_apply_hm` with `adm_dlm_weight`, `adm_min_val`,
  `adm_csf_scale`, `adm_noise_weight = 0`, `adm_p_norm = 1`; against the CPU
  extractor of the same build also `adm_f1s3` / `adm_f2s0` and
  `adm_norm_view_dist` with `adm_ref_display_height`.
- **Positive**: before, against the same reference: 59 of 336 outputs of the
  Netflix pair identical (2.5e-6), 1 of 21 and 4 of 21 on the two
  checkerboards (3.5e-7, 1.5e-7), 301 of 1400 on 200 BBB frames (1.7e-5).
- **Positive**: on near-flat content scored with `adm_noise_weight=0` the
  twin reports the CPU's `adm2 = 0` where it reported 1.
- **Positive**: `sycl_float_adm_math.h` was compared on the host with the
  reference's fp64 expressions on 1.8e9 samples of every magnitude,
  subnormals and the neighbourhood of the gain clamp included: none wrong.
  `test_sycl_float_adm_math` repeats that on 15 million samples per run on
  the host and 5 million in a kernel, and compares one scale's decouple, CSF
  and reductions with `adm_tools.c`'s routines.
- **Positive**: a 3840x2160 frame takes 12.3 ms instead of 15.1 ms. Through
  the `vmaf` tool on the A380, medians of 11 runs of 50 frames at host load 9
  to 11; 0.59 and 0.57 ms at 576x324; the `float_psnr` control read 3.27 and
  3.28 ms. One kernel fewer per scale (five, six before) and one buffer more:
  nine terms per sample of the reduced region, 48 MB of device memory at
  3840x2160. (The first version of this change, which read the reciprocal
  estimate from a table, took 15.8 ms.)
- **Negative**: `adm_p_norm` other than 1 or 3 is not identical: both sides
  raise every term with `powf`, the device's and the host's. Measured at 2,
  2.5 and 4.5: 1.8e-7 at most.
- **Negative**: the CPU extractor of an icx build is not the CPU extractor of
  a GCC build in its last digit. `adm_pool_bands_s()` calls `powf`, Intel's
  in one and glibc's in the other. Against the GCC build the twin's `aim`
  and `adm3` differ on 2 of 200 BBB frames by 1.6e-9 and 8.0e-10, and one of
  48 Netflix frames differs by 7.5e-8 with `adm_f1s3=2.25:adm_f2s0=0.3` and
  by 2.1e-10 with `adm_norm_view_dist=4.5:adm_ref_display_height=2160`;
  against the icx build's own CPU extractor all are identical
  (`T-ICX-LIBIMF-HOST-MATH-2026-10-01`).
- **Neutral / follow-ups**:
  - The twin no longer depends on the host processor: the decouple's
    quotient is the IEEE one on the CPU and on the device (ADR-1442). The
    first version of this change read the host's `RCPSS` estimate from a
    probed table; that path, its table and its upload are gone.
  - The header mirrors `adm_decouple_s()`, `adm_csf_s()`,
    `adm_cm_thresh3x3_s()`, `adm_csf_den_scale_s()` and `adm_cm_s()`. A
    change there changes `sycl_float_adm_math.h` and the CUDA twin's
    `float_adm_device.h` in the same PR;
    `test_sycl_float_adm_exact_contract.py` fails when the mirrored lines
    move.
  - `test_sycl_float_adm_parity` asserts equality on every output; it failed
    on the old twin from the first case.
    `core/test/float_adm_twin_parity.h` holds the fixtures and cases for any
    backend; `test_cuda_float_adm_parity.c` has the same cases inline and can
    move to it.
  - With `debug=true` and a feature-parameter option the CPU files the ratio
    under `adm` without the option suffix while its twins file it with the
    suffix (`T-FLOAT-ADM-DEBUG-KEY-UNSUFFIXED-2026-10-01`).
  - No kernel uses scratch memory (`test_sycl_kernel_scratch`, 118 kernels
    audited on the A380).
  - Frames below 17x17 are outside the guarantee
    (`T-FLOAT-ADM-TINY-FRAME-BAND-READS-2026-10-01`).
  - `float_adm_hip` and `float_adm_metal` keep the old arithmetic
    (`T-GPU-FLOAT-ADM-CPU-ARITHMETIC-2026-10-01`).

## References

- `req` (maintainer brief for the SYCL exactness lane, 2026-10-01): "results before speed; a twin reproduces the CPU bit for bit, tuning comes afterwards".
- [ADR-1420](1420-cuda-float-adm-cpu-arithmetic.md) (the CUDA twin and the
  reference exports), [ADR-1442](1442-float-adm-reference-divides.md) (the
  reference's quotient), [ADR-1432](1432-sycl-integer-vif-exact-gain.md) and
  [ADR-1422](1422-sycl-float-vif-cpu-arithmetic.md) (the replay technique and
  its primitives), [ADR-1411](1411-sycl-float-motion-cpu-float-sum.md) (the
  row-sequential sum), [ADR-1397](1397-psnr-hvs-twins-cpu-float-sum.md) (the
  exact cell), [ADR-1395](1395-sycl-kernels-no-scratch.md),
  [ADR-0220](0220-sycl-fp64-fallback.md),
  [ADR-1367](1367-sycl-strict-fp-every-feature-tu.md),
  [ADR-0202](0202-float-adm-cuda-sycl.md),
  [ADR-0214](0214-gpu-parity-ci-gate.md),
  [ADR-0024](0024-netflix-golden-preserved.md).
- [Research-1434](../research/1434-sycl-float-adm-fp64-free-arithmetic.md).
- `docs/state.md`: `T-SYCL-FLOAT-ADM-NOT-CPU-ARITHMETIC-2026-10-01` (closed
  by this decision).
