<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1472: The integer ADM weight limits follow from the contrast-masking cube and the largest wavelet coefficient of a scale

- **Status**: Accepted; narrows the scale 1 to 3 budget of [ADR-1325](1325-integer-adm-barten-fixed-point-normalization.md)
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `metrics`, `adm`, `correctness`, `cuda`, `sycl`, `hip`, `metal`, `rc3`, `fork-local`

## Context

ADR-1325 made `adm_csf_mode=1` (Barten) usable in the fixed-point `adm`
extractor: the CSF weights of one wavelet scale are divided by a shared power
of two until they fit a budget, and the contrast-masking finalisation restores
three times that exponent. The budget it chose was storage plus two bits:
below 2^16 at scale 0 and below 2^30 at scales 1 to 3.

That budget is too wide. Per sample the contrast-masking reduction forms the
excess `v` of the weighted coefficient over its masking threshold and then

```c
v_sq = (int32_t)((v * v + round) >> shift_sq);   /* shift_sq 29 or 30 */
val  = ((int64_t)v_sq * v + round) >> shift_cub;
```

(upstream's `ADM_CM_ACCUM_ROUND` / `I4_ADM_CM_ACCUM_ROUND`, the fork's
`adm_cm_accum_round()` / `i4_adm_cm_accum_round()` and their SIMD and device
twins). The square fits int32 only for `v <= 2^30 - 1` when the shift is 29
(scale 0, horizontal and vertical band) and for `v <= 1518500249` when it is
30 (scale 0 diagonal, scales 1 to 3). Above that it wraps: the cube turns
negative or small. Nothing reports it. A negative accumulator ends as NaN in
the p-norm and the frame fails (ADR-1302 guard); a positive one publishes a
wrong score.

With a weight just under 2^30 the weighted coefficient `(band * weight) >> 28`
is up to four times the coefficient, so a coefficient above 3.8e8 already
passes the limit, and one above 5.4e8 leaves int32 in the weighting itself.
Measured on master `ade338374`:

| Input, `adm=adm_csf_mode=1` | Result |
|---|---|
| 10 px checkerboard (`checkerboard_1920_1080_10_3_0_0` / `_10_0`) | fails every frame: `aim_num=-nan`. The square of the scale-3 diagonal sample at (8, 13) is 2246297208 |
| 1 px checkerboard (`_0_0` / `_1_0`) | `integer_adm2` 0.587102 for frame 0, no message. `float_adm` gives 0.783557 |
| the same pair with `adm_csf_scale=1.2` (the open row's reproducer) | fails every frame: a scale-1 vertical square of 2787428024 |
| Netflix 576x324 pair | correct; no sample comes near the limit |

`T-ADM-AIM-BARTEN-SCALE-TERM-WRAP-2026-10-01` recorded the third line and
assumed the frame always fails loudly. The second line shows it does not.

Upstream Netflix/vmaf (`cea2b4d83`, 2026-10-01) has the same reduction and
the same `adm_csf_mode` option, without any weight normalisation: its Barten
weights wrap in the `(uint16_t)` / `(uint32_t)` conversions, and
`integer_adm2` is 0.0027 on the Netflix pair where `float_adm` gives 0.9654.
Its default Watson97 weights are inside the budget derived below.

## Decision

A fixed-point CSF weight is in budget when the largest wavelet coefficient
its scale can produce, weighted, still has a square that fits int32.
`adm_csf_fixed_limit(scale, band)` in `core/src/feature/adm_csf_fixed_point.h`
returns that limit and `adm_csf_fixed_scale()` normalises against it.

The largest coefficient follows from the filter taps, whatever the picture. A
detail band is a linear function of the centred pixel `p / 2^bpc - 1/2`, which
lies in [-1/2, 1/2), so its magnitude is at most half the absolute sum of the
composite two-dimensional filter, in the band's fixed-point format:

| Scale | Band format | Half the absolute filter sum (h, v / d) | Bound | Constant | Reached by the 8-bit sign-pattern frame |
|---|---|---|---|---|---|
| 0 | 2^14 | 1.3995 / 1.3995 | 22929.4 | 23040 | 22840 |
| 1 | 2^29 | 2.6990 / 2.6222 | 1448980000 | 1456000000 | 1443331337 |
| 2 | 2^27 | 5.5902 / 5.5992 | 751508000 | 755000000 | 748575616 |
| 3 | 2^26 | 11.0643 / 10.9736 | 742509000 | 746000000 | 739619691 |

The constants round the bounds up by about half a percent, which covers the
pipeline's rounding and the decouple stage's reciprocal table. The excess is
at most the weighted coefficient, plus 28 at scales 1 to 3 (the threshold
there can be as low as -27, ADR-0155's rounding term, and the weighting
rounds). The limits are therefore

- scale 0, horizontal and vertical: `(2^30 - 1) / 23040` = 46603.4 (was 65536);
- scale 0, diagonal: 65536, the `uint16_t` storage (65535 * 23040 is below
  1518500249);
- scale 1: `(1518500249 - 28) * 2^28 / 1456000000` = 279958309 (was 2^30);
- scale 2: 539893111; scale 3: 546406567 (both were 2^30).

Everything else of ADR-1325 stands: one exponent for the three bands of a
scale, the smallest that fits, restored as `3k` after the cube; scalar
weighted-CSF and contrast-masking stages when any `k` is non-zero.

The same budget keeps the weighted CSF bands (`i4_adm_csf`, int32) and the
scale-0 CSF bands (int16 after a shift of 15) in range, and the masking
threshold sum in int32.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Per-scale limits from the worst coefficient (chosen) | Holds for every picture; one header, so CPU, CUDA, HIP, SYCL and Metal follow without a kernel change; Watson97 and both blend modes keep `k = 0` and their bits | Barten-mode weights lose one or two bits at scales 1 to 3: outputs that were already correct move by up to 7.7e-7 on the Netflix pair | Chosen |
| Keep 2^30 and widen the square to int64 in every implementation | Bits unchanged wherever today's result is right | Not sufficient: under a weight of 2^30 the weighted coefficient itself (up to 5.8e9) leaves int32, and with a wider sample the cube leaves int64 (2^35 squared, shifted by 30, times 2^35). Needs 128-bit products in scalar C, AVX2, AVX-512, CUDA, HIP, SYCL and Metal | Cannot be made complete in 64 bits |
| Choose the exponent per frame from the frame's largest coefficient | Keeps full precision on ordinary content | The exponent would depend on the picture: a device twin would need a reduction pass before the weighting, results of one frame would depend on precision chosen from its content, and the SIMD / scalar choice would change mid-stream | Complexity and a content-dependent metric definition for no measurable gain |
| Saturate the excess at the square's limit | Smallest change | Publishes a plausible but wrong score for the samples it clamps, the outcome ADR-1325 rejected for the weights | Silent metric substitution |
| Power-of-two limits (2^28, 2^29, 2^29) | Easy to read | Tighter than needed at scale 1 by 4 % and no simpler to test; scale 0 needs a non-power-of-two limit anyway because the upstream-tabulated default weight 36453 lies above 2^15 | The derived values are as easy to check and keep more precision |

## Consequences

- **Positive**: no frame can wrap the contrast-masking square, at any bit
  depth, in any CSF mode. The four reproducers above score, and agree with
  `float_adm` to 1e-5 or better.
- **Positive**: every twin follows through the shared header. Measured on an
  RTX 4090 (CUDA), a gfx1036 (HIP) and an Arc A380 (SYCL): `adm` equals
  `--backend cpu` on every output under the default, three Barten and one
  blend configuration, on the Netflix pair, both checkerboards and six
  adversarial frames. Metal takes the weights from the same function and is
  not measured here.
- **Negative**: Barten-mode `adm` outputs change. The default Barten
  configuration goes from exponents 5, 6, 7 to 7, 7, 8 at scales 1, 2, 3;
  on the Netflix pair `integer_adm2` moves by at most 2.1e-7 and
  `integer_aim` by at most 7.7e-7. Watson97 (every viewing geometry: its
  weights are largest at the minimum geometry, which is the default) and the
  two blend modes are unchanged, so no Netflix golden value moves.
- **Neutral / follow-ups**:
  - `core/test/test_integer_adm_cm_budget.c` derives the bounds again from
    the wavelet taps, checks the limits against them from both sides, and
    scores five adversarial frames against `float_adm`.
  - A change to the wavelet taps, to a DWT shift, to `shift_sq` or to
    `i4_shift_dst` changes the bounds: the test fails until the constants
    follow.
  - Upstream: the reduction is upstream's, so a port of the fork's
    normalisation to Netflix/vmaf needs these limits, not 2^30.

## References

- `req` (coordinator brief, 2026-10-02, paraphrased): establish the cause of
  `aim_num=-nan` for integer `adm` with `adm_csf_mode=1` on the 10 px
  checkerboard with the smallest reproducer, check upstream at the mapped
  path, fix it with a test that fails before, keep the golden gate and
  re-prove the `adm` twin cells.
- `docs/state.md`: `T-ADM-AIM-BARTEN-SCALE-TERM-WRAP-2026-10-01`.
- [ADR-1325](1325-integer-adm-barten-fixed-point-normalization.md) (the
  normalisation), [ADR-1191](1191-adm-csf-fixed-point-representability-guard.md),
  [ADR-0155](0155-adm-i4-rounding-deferred-netflix-955.md) (the rounding term that makes
  a threshold negative), [ADR-1302](1302-nonfinite-scores-fail-the-frame.md),
  [ADR-1416](1416-cuda-adm-cpu-row-rounding.md).
- [Research-1472](../research/1472-integer-adm-cm-weight-budget.md).
