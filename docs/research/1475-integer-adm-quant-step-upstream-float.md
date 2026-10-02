<!-- markdownlint-disable MD013 MD060 -->
# Research-1475: the fork's integer ADM against Netflix master, before and after the quantisation step went back to `float`

- **Date**: 2026-10-02
- **Companion ADR**: [ADR-1475](../adr/1475-integer-adm-quant-step-upstream-float.md)
- **Rows**: `T-UPSTREAM-AB-SCORE-DELTA-2026-09-07`, `T-ADM-CSF-EXPONENT-NOT-UPSTREAM-2026-10-01`

## Question

Does the fork's CPU return Netflix master's bits for integer ADM and for the
models that read it, and if not, which expression is responsible?

## Method

- Netflix/vmaf `cea2b4d8` and the fork, both built with GCC 16.2.1, meson
  release, `-Denable_float=true`, x86-64 without `-march`. Netflix compiles
  with `-O3 -std=c11`, the fork with `-O3 -std=c23 -ffp-contract=off`; both
  are ISO modes, and neither `libvmaf.so` has a fused multiply-add outside a
  function named `*_avx2` / `*_avx512`.
- One C program per tree, linked against that tree's `libvmaf.a`, drives the
  public API (`vmaf_use_feature()`, `vmaf_model_load_from_path()`,
  `vmaf_read_pictures()`) and prints every value the feature collector holds
  with `%.17g`. Netflix's CLI prints six decimals, which hides this defect.
- Dispatch: `cpumask` 63 (scalar), 48 (AVX2 only) and 0 (the host's AVX-512).
- 31 fixtures: the Netflix 576x324 pair at 8, 10, 12 and 16 bits and as 10-bit
  4:2:2, both 1920x1080 checkerboard pairs, Big Buck Bunny at 1920x1080 and
  3840x2160 (48 frames each), Sparks, synthetic noise at 8 to 16 bits, a bright
  16-bit frame, two gradients, 4:4:4 and 4:0:0 clips, and frames from 352x288
  down to 8x8. The `adm` extractor with `debug=true` and 19 option variants;
  25 model files, which are byte-identical in the two trees.

## Result

Values identical to Netflix / values compared, with the largest absolute
difference:

| | Before (`39929960f`) | After |
|---|---|---|
| `integer_adm2`, scalar | 52 / 658, 8.5e-8 on decoded pictures | 616 / 658 |
| `integer_adm2`, AVX2 | 52 / 658 | 636 / 658 |
| `integer_adm2`, AVX-512 | 52 / 658 | 632 / 658 |
| `integer_adm3`, scalar | 4 / 658, 5.5e-8 | 616 / 658 |
| `integer_adm_scale1`, `integer_adm_scale2` | 40 and 118 / 658, 2.5e-7 | 658 / 658 |
| `vmaf_v0.6.1`, AVX-512 | 32 / 504, 1.83e-5 | 504 / 504 |
| `vmaf_v0.6.1`, AVX2 | 32 / 504, 1.83e-5 | 504 / 504 |
| `vmaf_v0.6.1`, scalar | 32 / 504 | 498 / 504 |
| `vmaf_v0.6.1neg`, AVX-512 | 18 / 504, 1.81e-5 | 504 / 504 |
| `vmaf_4k_v0.6.1`, AVX-512 | 32 / 504, 1.49e-5 | 504 / 504 |
| `vmaf_b_v0.6.3` (all bootstrap outputs), AVX-512 | 418 / 6048, 2.11e-5 | 6048 / 6048 |
| option variants of `adm` identical at scalar and AVX-512 | 5 of 19 | 8 of 19 |

What still differs after the change is deliberate, or Netflix's own code
disagreeing with itself:

- The four noise fixtures (`integer_adm_scale0`, and through it `adm2` and
  `adm3`): the fork keeps the scale-0 masking centre tap in int32
  ([ADR-1402](../adr/1402-adm-cm-centre-tap-int32.md)). Netflix's scalar and
  vector paths differ from each other there by the same amounts (`adm2` up to
  9.4e-5), which is why the scalar column has more differing frames than the
  vector ones and why six frames of `vmaf_v0.6.1` remain at scalar dispatch.
- Frames of 17 to 24 pixels (scale 3), where Netflix reads outside the band.
- Of the option variants: `adm_csf_mode=1` (the Barten weights,
  [ADR-1472](../adr/1472-integer-adm-cm-weight-budget.md)), and
  `adm_enhn_gain_limit=1.2` at vector dispatch
  ([ADR-1413](../adr/1413-adm-gain-limit-truncated-double-product.md)); the
  other eight differ on the 8-bit noise fixture at scalar dispatch only.

The prediction is not involved: with identical features the two trees return
identical scores for every model, the bootstrap models included.

## The expression

`dwt_quant_step()` returns `2 a 10^(k temp^2) / amplitude`. Netflix forms
`k * temp * temp` in `float`; the fork formed it in `double` since PR #552.
For the default viewing geometry the reciprocal weights compare as follows
(the fork's first): scale 0 `0x1.1cc772p-6` and `0x1.1cc774p-6`, its diagonal
`0x1.820d4ep-8` and `0x1.820d54p-8`, scale 1 `0x1.060508p-5` and
`0x1.06050cp-5`. `core/test/test_integer_adm_quant_step.c` holds the step's
bits from a Netflix build for five geometries.

## Twins and snapshots

- `adm_cuda` (RTX 4090), `adm_hip` (gfx1036), `adm_sycl` (Arc A380): the
  parity tests assert equality and pass; the parity gate's `adm` cell is
  compared at tolerance 0 on the Netflix pair, both checkerboard pairs and Big
  Buck Bunny 1920x1080.
- `testdata/scores_cpu_{576,640,720,1080,4k}.json`: a build of the previous
  master reproduces all five files value for value on clips made with
  `testdata/generate.sh`'s commands; with the change 59, 41, 44, 45 and 38 of
  720 values move, by at most 2e-5.
  The committed values are those of the golden build profile
  (`scripts/ci/setup-golden-build.sh`) inside the dev container: GCC 15.2.0,
  glibc 2.43, Ubuntu 26.04, image
  `sha256:43ef1e32cb32b148a076ed6dff73b72d7a6566ca3882bf90954b8a34a74761fc`.
  A host build of the same profile with GCC 16.2.1 and glibc 2.44 returns the
  same values at `%.17g`: 0 of 3600 per-frame values and no pooled value
  differ over the five files, so for this model the result does not depend on
  which of the two compilers and C libraries built it.
- A build with Intel's compiler: the step has the same bits as under GCC and
  glibc (the test above passes in an icx build). One of 240 frames at the five
  snapshot resolutions then differs between an icx and a GCC build in
  `integer_adm_scale1` (7.9e-8), because Intel's `powf` rounds that frame's
  argument differently; with glibc's `libm` loaded the icx-built library
  returns the GCC build's values.

## Reproduce

```bash
testdata/bench_upstream_ab.py --runs 1 --max-score-delta 0 \
    --upstream-bin <a Netflix/vmaf cea2b4d8 build>/tools/vmaf
python3 scripts/ci/run_meson_test.py -- -C build test_integer_adm_quant_step \
    test_integer_adm_quant_step_contract
```
