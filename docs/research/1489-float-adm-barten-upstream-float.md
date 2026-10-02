<!-- markdownlint-disable MD013 MD060 -->
# Research-1489: the fork's float ADM against Netflix master, before and after its CSF weights went back to `float`

- **Date**: 2026-10-02
- **Companion ADR**: [ADR-1489](../adr/1489-float-adm-barten-upstream-float.md)
- **Rows**: `T-ADM-CSF-EXPONENT-NOT-UPSTREAM-2026-10-01`

## Question

Which expressions separate the fork's `float_adm` from Netflix master's, and
once the unintended ones are removed, is the division of
[ADR-1442](../adr/1442-float-adm-reference-divides.md) the only one left?

## Method

- The harness of [Research-1475](1475-integer-adm-quant-step-upstream-float.md):
  Netflix/vmaf `cea2b4d8` and the fork, both built with GCC 16.2.1 (meson
  release, `-Denable_float=true`, x86-64 without `-march`), one C program per
  tree linked against that tree's `libvmaf.a`, every value of the feature
  collector printed with `%.17g`, `cpumask` 63 (scalar), 48 (AVX2) and 0
  (AVX-512).
- 31 fixtures, 658 frames in the 28 that `float_adm` accepts (the fork refuses
  the three below 17x17, PR #1770). `float_adm` by default, with `debug=true`
  and under 36 option variants (`adm_csf_mode` 1 to 9, the per-scale factors,
  the gain limit, the viewing geometry, `adm_p_norm`, the skip options); the
  seven models that read `float_adm`.
- A third tree, **Netflix with the plain quotient**: `cea2b4d8` with
  `ADM_OPT_RECIP_DIVISION` undefined in `adm_options.h` and the
  `#ifdef __SSE2__` of `adm_tools.c` forced to its `#else` branch
  (`#define DIVS(n, d) ((n) / (d))`, what Netflix compiles on ARM). The
  second edit is needed because with the macro alone undefined the file has
  no `DIVS` on x86 and does not compile. Nothing else differs from
  `cea2b4d8`.
- Two probes outside the harness print the bits of `dwt_quant_step()` (five
  viewing geometries, four scales, two orientations: 40 values) and of
  `barten_csf()` (four geometries, twelve luminance levels from 0.001 to 155
  cd/m2, three values of `adm_csf_scale`: 144 argument sets, four scales each)
  from each tree's headers, compiled as C and, for the Barten header, as C++.

## Result

The two routines:

| Probe | Master before | This change |
|---|---|---|
| `dwt_quant_step()`, 40 steps: bits equal to Netflix's | 8 | 40 |
| `barten_csf()`, 144 argument sets: all four scales equal to Netflix's | 6 | 144 |
| the same header compiled as C++ against its own C value | 144 | 144 |
| Netflix's text of the header compiled as C++ against Netflix's C value | | 9 of 144 |

The last line is why the fix writes the promotions out: in C++
`pow(float, float)` and `exp(float)` are the `float` functions.

Values identical / values compared, the same at scalar, AVX2 and AVX-512
dispatch:

| | Before, against Netflix | After, against Netflix | After, against Netflix with the plain quotient |
|---|---|---|---|
| `adm2`, 658 frames | 30, at most 1.14e-7 apart | 464, at most 6.8e-8 | 658 |
| per-frame values of the default and `debug=true` runs | 1867 / 8225 | 7053 / 8225 | 8225 / 8225 |
| per-frame values of 36 option variants | 17 995 / 71 280 | 60 085 / 71 280 | 71 280 / 71 280 |
| `vmaf_float_v0.6.1` score, 252 frames | 9, at most 2.0e-5 | 179, at most 1.48e-5 | 252 |
| `vmaf_float_v0.6.1neg` score | 13, at most 2.7e-5 | 151, at most 1.79e-5 | 252 |
| `vmaf_float_4k_v0.6.1` score | 9, at most 1.64e-5 | 179, at most 1.2e-5 | 252 |
| `vmaf_v0.6.0` score | 9, at most 1.83e-5 | 179, at most 1.34e-5 | 252 |
| frame scores of the seven models | 146 / 1764 | 1243 / 1764 | 1764 / 1764 |

So the residual against Netflix as built on x86 is the division alone: remove
it from Netflix and nothing differs. What remains outside the table is the
three fixtures below 17x17, which the fork refuses.

How far the fork's own scores move (tree before against tree after):

| Metric | Frames that move | Largest move |
|---|---|---|
| `float_adm` `adm2` | 618 of 658 | 1.14e-7 |
| `float_adm` per-scale scores | | 2.7e-7 |
| `float_adm` `adm2`, `adm_csf_mode=1` | 315 of 315 | 1.5e-7 |
| integer `adm` `integer_adm2`, `adm_csf_mode=1` | 315 of 315 | 1.6e-7 |
| integer `adm` per-scale scores, `adm_csf_mode=1` | | 2.5e-7 |
| `vmaf_float_v0.6.1` frame score | 241 of 252 | 2.2e-5 |
| `vmaf_float_v0.6.1neg` frame score | 245 of 252 | 2.7e-5 |
| pooled score of a float model | | 1.5e-5 |

Integer `adm` outside Barten mode and the models that do not read `float_adm`
do not move: 82 710 of 82 710 values are identical (`adm` by default and
under 17 option variants, 11 models). No other extractor includes either
header.

## Twins

- `float_adm_cuda` (RTX 4090), `float_adm_hip` (gfx1036), `float_adm_sycl`
  (Arc A380) take the weights from the CPU's `adm_csf_rfactor_s()`. Their
  parity tests assert equality and pass; the parity gate compares `float_adm`
  and `adm` at tolerance 0 on the Netflix pair, both checkerboard pairs and
  Big Buck Bunny 1920x1080: 0 on all 24 cells.
- Integer `adm` with `adm_csf_mode=1` on each twin against the CPU extractor of
  the same build, `debug=true`, `--precision max`: 0 of 864 values differ on
  the Netflix pair and 0 of 864 on Big Buck Bunny 1920x1080, per backend. For
  SYCL this is the C++ translation unit's copy of the Barten weights against
  the C one's.
- An icx build (Intel's math library): `test_float_adm_csf_upstream` passes,
  bit table included.
- Metal: `float_adm_metal.mm` carries the edited step; not run (no device).

## Reproduce

```bash
python3 scripts/ci/run_meson_test.py -- -C build test_float_adm_csf_upstream \
    test_float_adm_csf_upstream_contract test_float_adm_x86 test_barten_csf
# Netflix with the plain quotient, to compare float_adm against:
#   libvmaf/src/feature/adm_options.h: remove `#define ADM_OPT_RECIP_DIVISION`
#   libvmaf/src/feature/adm_tools.c:   `#ifdef __SSE2__` -> `#if 0`
```
