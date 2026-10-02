<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1489: The CSF weights of float ADM are Netflix's float arithmetic again, so `float_adm` differs from Netflix by the division alone

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `numerics`, `adm`, `netflix-compat`, `upstream-divergence`, `codeql`, `sycl`, `metal`, `gpu-parity`, `testing`, `fork-local`

## Context

The maintainer decided on 2026-10-02 what the reference for inherited code is:
Netflix's source. Every expression the fork inherited from Netflix/vmaf
evaluates as Netflix master evaluates it, unless an ADR records why the fork
differs. [ADR-1475](1475-integer-adm-quant-step-upstream-float.md) applied
that to the quantisation step of integer ADM. Float ADM has the same step in
its own header, and a second source of CSF weights, the Barten model; both
had been widened from `float` to `double` by fork commits that record no
decision.

**The Watson step.** `dwt_quant_step()` in `adm_tools.h` stores the display
resolution `r` and the logarithm `temp` in `float` and raises 10 to the
`float` product `k * temp * temp`
(Netflix `libvmaf/src/feature/adm_tools.h`, lines 336 to 347 at `cea2b4d8`).
PR #552 (`9ce9ab86a`, 2026-05-09), the static-analysis sweep ADR-1475
describes, cast one operand of the product to `double`. PR #760
(`6435a88f4`, 2026-05-11), a port of two upstream commits, went further while
it touched the function's signature: it declared `r`, `temp` and `Q` as
`double` and narrowed on return, with a comment that names the same
static-analysis finding (`cpp/integer-multiplication-cast-to-long`) as its
reason. 32 of 40 probed steps (five viewing geometries, four scales, two
orientations) left Netflix's bits.

**The Barten CSF.** `barten_csf_tools.h` computes the weights of
`adm_csf_mode=1`. Netflix forms six products and quotients of `float`s in
`float` and hands the results to `pow()` and `exp()`: the interpolation slope
`(right - left) / (right_position - left_position)`, `p_0 * f`, `f / 7`,
`a * b`, `-b[i] * f`, and the product of the three CSF factors. PR #44
(`d06dd6cfc`, 2026-04-18), which ported the header, added a `(double)` to one
operand of each "to silence" the same finding. Its message calls the casts
semantics-preserving on the evidence of the default model's score on the
Netflix pair, a run that never calls `barten_csf()`. The golden assertions
that do use the Barten mode compare at four to six decimals and pass either
way. 138 of 144 probed argument sets (four geometries, twelve luminance
levels, three scales) return other bits than Netflix's for at least one
wavelet scale.

Neither change was intended as a numerical deviation: both answer a lint
finding, neither has an ADR, and nothing in the fork needs the wider
intermediates.

Measured against Netflix master with the harness of ADR-1475 (C API, every
collector value at `%.17g`): on 658 frames of 28 fixtures the fork's `float_adm`
equalled Netflix's `adm2` on 30 frames (at most 1.14e-7 apart; the per-scale
scores up to 2.7e-7), on 1867 of the 8225 per-frame values of the default and
`debug=true` runs, and on 17 995 of 71 280 values over 36 option variants. `vmaf_float_v0.6.1` equalled Netflix's score on 9 of 252
frames (at most 2.0e-5 apart), `vmaf_float_v0.6.1neg` on 13 (2.7e-5). The
counts are the same at scalar, AVX2 and AVX-512 dispatch. A part of that
distance is the division of
[ADR-1442](1442-float-adm-reference-divides.md), which is deliberate; the
rest is the two routines above.

One property of the Barten header matters for the form of the fix. The header
is compiled as C by the CPU extractors and as C++ by the SYCL and Metal twins
of integer ADM. In C++ `pow(float, float)` and `exp(float)` are the `float`
functions; in C they are the `double` ones. Netflix's text leaves the
promotion of each `float` argument to the language, so compiled as C++ it
returns other values than compiled as C: 135 of the 144 argument sets
differ. The fork's `double` operands had hidden that, because a `double`
argument selects the `double` function in both languages.

## Decision

Both routines evaluate Netflix's arithmetic.

- `dwt_quant_step()` in `core/src/feature/adm_tools.h` is Netflix's three
  statements: `float r`, `float temp`, and
  `float Q = 2.0 * params->a * pow(10.0, params->k * temp * temp) / ...`. The
  static-analysis finding is answered with a suppression that cites this ADR.
- `core/src/feature/barten_csf_tools.h` forms each product and quotient in
  `float`, as Netflix does, and writes the promotion of the result out:
  `pow((double)(p_0 * spatial_frequency), (double)p_1)`,
  `exp((double)(-barten_mtf_params_b[i] * spatial_frequency))` and so on. An
  explicit conversion of a `float` result is the value C's implicit promotion
  gives, selects the `double` math function in C++ as well, and is not what
  the static-analysis query reports. `linear_interpolate()` is Netflix's text
  unchanged (no math function is involved).
- The Metal twin of float ADM keeps a copy of the step
  (`core/src/feature/metal/float_adm_metal.mm::fadm_dwt_quant_step()`), which
  had the `double` product; it forms the exponent in a named `float` now. The
  CUDA, HIP and SYCL twins call `adm_csf_rfactor_s()` and follow by rebuild.

`ADM_OPT_RECIP_DIVISION` stays undefined
([ADR-1442](1442-float-adm-reference-divides.md)): the fork divides where
Netflix multiplies by a reciprocal estimate on x86. With this change that is
the one remaining difference between the fork's `float_adm` and Netflix's.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Keep the `double` intermediates and record them here as a deliberate deviation | No score moves | `float_adm` and the float models differ from Netflix on most frames for no reason anyone chose | A deviation needs a reason; these came from a lint finding |
| Restore Netflix's text of `barten_csf_tools.h` verbatim | Smallest diff against upstream | Compiled as C++ it returns other weights (135 of 144 probed sets), so `adm_sycl` and `adm_metal` in Barten mode would leave the CPU; the finding returns and the next sweep widens an operand again | The twins are declared exact; one header must give one value |
| Verbatim text, and give the two C++ twins a C translation unit that computes the weights | Upstream's text untouched | A new translation unit per twin and a second way to reach the weights, for a difference that six explicit casts remove | Larger than the defect |
| Also restore the reciprocal division, to equal Netflix on x86 bit for bit | `float_adm` identical to Netflix on this processor | The scores would depend on the processor model again | Decided otherwise in ADR-1442, which stands |

## Consequences

- **Positive**: against Netflix master as it builds on x86 (reciprocal
  division), `adm2` is identical on 464 of 658 frames (30 before) and never
  more than 6.8e-8 apart; 7053 of 8225 default-run values and 60 085 of
  71 280 option-variant values are identical; `vmaf_float_v0.6.1` on 179 of
  252 frames (9 before), at most 1.48e-5 apart.
- **Positive**: against Netflix master built with the plain quotient,
  `float_adm` is identical on every value: 8225 of 8225 default-run values and
  71 280 of 71 280 over the 36 option variants (the Barten mode among them),
  at scalar, AVX2 and AVX-512 dispatch. The division is therefore the only
  arithmetic difference left. The probe build is Netflix `cea2b4d8` with
  `ADM_OPT_RECIP_DIVISION` undefined and `adm_tools.c` taking its `#else`
  definition `DIVS(n, d) ((n) / (d))`, the one Netflix compiles on ARM;
  undefining the macro alone does not compile on x86, where the file then has
  no `DIVS` at all. Three fixtures below 17x17 are outside these counts: the
  fork refuses such frames (PR #1770) where Netflix reads outside the band.
- **Positive**: the seven models of the harness that read `float_adm`
  (`vmaf_float_v0.6.1`, its `.noclip` and `.transform` variants,
  `vmaf_float_v0.6.1neg`, `vmaf_float_4k_v0.6.1`, `vmaf_v0.6.0`,
  `nflxtrain_norm_type_none`) return the plain-quotient Netflix build's score
  on 1764 of 1764 frames (153 before).
- **Negative**: every `float_adm` score moves, and every score of a model
  that reads one. Measured between the tree before and after: `adm2` on 618 of
  658 frames by at most 1.14e-7, the per-scale scores by at most 2.7e-7; in
  Barten mode `adm2` on every one of 315 frames by at most 1.5e-7. The frame
  score of `vmaf_float_v0.6.1` moves by at most 2.2e-5, of
  `vmaf_float_v0.6.1neg` by 2.7e-5, a clip's mean by at most 1.5e-5.
- **Negative**: integer `adm` with `adm_csf_mode=1` moves as well, because its
  weights come from the same header. `integer_adm2` moves on every one of 315 measured frames by at
  most 1.6e-7, the per-scale scores by at most 2.5e-7. Against Netflix that
  mode stays apart by design: Netflix's Barten weights wrap in their
  fixed-point conversion and the fork normalises them (ADR-1325, ADR-1472). The default mode
  (Watson) and the blended modes read tables or the step of ADR-1475 and do
  not move. No snapshot under `testdata/` holds a Barten-mode or a `float_adm`
  value; none is regenerated.
- **Neutral**: the Netflix golden gate passes before and after (271 passed, 12
  skipped, x86-64 and aarch64).
- **Neutral**: the x86 kernels of float ADM
  ([ADR-1473](1473-float-adm-x86-simd-exact-and-dispatched.md)) take the
  weights as arguments and return the scalar code's bits with the new weights
  as with the old ones: `test_float_adm_x86` passes, and the scalar, AVX2 and
  AVX-512 outputs of the harness are identical.
- **Neutral**: the exact twins stay exact. `float_adm_cuda`, `float_adm_hip`
  and `float_adm_sycl` call the CPU's `adm_csf_rfactor_s()`; on an RTX 4090,
  a gfx1036 and an Arc A380 their parity tests and the gate's `float_adm` and
  `adm` cells hold at tolerance 0, and integer `adm` in Barten mode on each
  twin equals the CPU extractor of the same build on every value. The Metal
  copies are edited and not run (no device).
- **Neutral**: the weight limits of
  [ADR-1472](1472-integer-adm-cm-weight-budget.md) and the normalisation of
  [ADR-1325](1325-integer-adm-barten-fixed-point-normalization.md) are
  unchanged; they bound whatever weight the header returns.
- **Neutral**: the header is touched, so it is left lint-clean: its 18 locals
  that are never reassigned are `const`, which clears the 18
  `misc-const-correctness` findings the header had as C++; the sycl lane's
  clang-tidy baseline drops by 18. The qualifiers change no value.
- **Replaces**: the comment PR #44 left in `barten_csf_tools.h` ("fork-only
  deviation from upstream ... to silence
  cpp/integer-multiplication-cast-to-long") and the one PR #760 left in
  `adm_tools.h` ("Compute in double and narrow to float on return"). No
  accepted ADR stated either as a decision.
  [ADR-1420](1420-cuda-float-adm-cpu-arithmetic.md),
  [ADR-1434](1434-sycl-float-adm-cpu-arithmetic.md) and
  [ADR-1458](1458-hip-float-adm-cpu-arithmetic.md) say the twins take their
  CSF weights from the CPU's routine; that stands, and is why they follow.

## References

- `req` (popup answer, 2026-10-02): "Netflix's source, deviations only by ADR"
- [ADR-1475](1475-integer-adm-quant-step-upstream-float.md),
  [ADR-1442](1442-float-adm-reference-divides.md),
  [ADR-1472](1472-integer-adm-cm-weight-budget.md),
  [ADR-1473](1473-float-adm-x86-simd-exact-and-dispatched.md),
  [ADR-1325](1325-integer-adm-barten-fixed-point-normalization.md)
- [Research-1489](../research/1489-float-adm-barten-upstream-float.md)
- Netflix/vmaf `cea2b4d8`, `libvmaf/src/feature/adm_tools.h`
  (`dwt_quant_step()`) and `libvmaf/src/feature/barten_csf_tools.h`
- PR #552 (`9ce9ab86a`), PR #760 (`6435a88f4`), PR #44 (`d06dd6cfc`)
