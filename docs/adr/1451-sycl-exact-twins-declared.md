<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1451: `adm_sycl`, `motion_sycl`, `motion_v2_sycl`, `psnr_sycl`, `float_ssim_sycl` and `cambi_sycl` are declared exact twins; `speed_chroma_sycl` is not

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `sycl`, `gpu-parity`, `numerics`, `testing`, `ci`, `rc3`, `fork-local`

## Context

RC3 asks every GPU twin to return the CPU extractor's bits
([ADR-1421](1421-rc3-rc8-candidate-map.md)). On SYCL eleven gate features
were declared exact, each by the ADR that made its twin so (`vif`, `ssim`,
`float_adm`, `float_motion`, `float_ms_ssim`, `float_ms_ssim_lcs`,
`float_vif`, `float_moment`, `float_psnr`, `psnr_hvs`, `ssimulacra2`). The
other SYCL cells of the parity gate were compared with a tolerance of 5e-5,
whatever the twin returned. A twin that is bit-identical and compared with
5e-5 can lose its last eleven digits without a gate noticing
([ADR-1437](1437-hip-exact-twins-declared.md) made the same observation for
HIP).

A sweep on an Arc A380 (xe driver) ran every undeclared gate feature through
the gate at `--precision max`, `--backend cpu` against `--backend sycl` of
one build, on fourteen fixtures: the Netflix 576x324 pair at 8, 10, 12 and 16
bits and as 10-bit 4:2:2 (48 frames), both 1920x1080 checkerboard pairs,
independent full-range noise at 576x324 and 8, 10, 12 and 16 bits, a bright
16-bit 1920x1080 pair, BBB 3840x2160 widened to 16 bits (8 frames) and 200
frames of BBB 3840x2160: 333 frames. The noise, the bright pair and the
widened clip are the fixtures that showed `float_moment_sycl` and
`float_psnr_sycl` to be exact only on small differences
([ADR-1449](1449-sycl-float-moment-cpu-float-squares.md),
[ADR-1450](1450-sycl-float-psnr-exact-block-sums.md)).

| Gate feature | Frames identical | Max abs diff |
|---|---|---|
| `adm` | 333 of 333 | 0 |
| `motion`, `motion_debug` | 333 of 333 | 0 |
| `motion_v2` | 333 of 333 | 0 |
| `psnr` | 333 of 333 | 0 |
| `float_ssim`, `float_ssim_lcs` | 333 of 333 | 0 |
| `cambi` | 333 of 333 | 0 |
| `speed_chroma` | 333 of 333 | 0 |
| `ciede` | twelve of thirteen fixtures (BBB 3840x2160 not run) | 1.0e-11 on the bright 16-bit pair, inside its libm bound ([ADR-1436](1436-sycl-ciede-cpu-arithmetic.md)) |

## Decision

We will declare a SYCL twin exact when it reaches the CPU's value by
construction and the sweep measured it identical on every frame, and list no
twin on measurement alone (the rule of ADR-1437). That admits six twins,
eight gate features:

| Gate feature | Twin | Why its value is the CPU's |
|---|---|---|
| `adm` | `adm_sycl` | Integer DWT, decouple and contrast sums on the device; the host finalises every output in the CPU's float arithmetic ([ADR-1362](1362-sycl-integer-adm-aim-device-pass.md)) |
| `motion`, `motion_debug` | `motion_sycl` | Integer SAD of the differenced, filtered frame with the CPU's rounding per pass; the host weights, caps and blends as `integer_motion.c` does ([ADR-1371](1371-sycl-motion-diff-first-pipeline.md)) |
| `motion_v2` | `motion_v2_sycl` | The same kernel and the CPU flush's formula (ADR-1371) |
| `psnr` | `psnr_sycl` | Integer SSE per plane; the host concludes through `psnr_score.h`, the CPU's helpers ([ADR-1365](1365-sycl-twin-cpu-option-parity.md)) |
| `float_ssim`, `float_ssim_lcs` | `float_ssim_sycl` | The CPU's decimation bit for bit ([ADR-1370](1370-sycl-float-ssim-device-decimation.md)) and the CPU's window arithmetic type for type with integer frame sums (`sycl_ssim_terms.h`, [ADR-1414](1414-sycl-float-ms-ssim-cpu-arithmetic.md)); the frame mean is rounded to `float` as `iqa_ssim()` rounds it |
| `cambi` | `cambi_sycl` | Integer pipeline, fp32 c-values that are `cambi.c`'s, an exact fixed-point top-K sum ([ADR-1357](1357-sycl-cambi-device-resident.md)) |

One fragment file per feature under `scripts/ci/exact_twins.d/`
([ADR-1428](1428-exact-twins-fragments.md)) lists `sycl` for these features,
so the parity gate compares each cell with tolerance 0 at `--precision max`.
`test_sycl_exact_twins` holds all six to `==` on a device.

`speed_chroma_sycl` is not listed. Its kernels reproduce `speed.c` operation
for operation ([ADR-1358](1358-sycl-speed-device-resident-linalg.md)), and
the sweep found it identical, but its `log2` is a correctly rounded
evaluation on the device where the CPU calls the build's `log2f`. Equality
then depends on the math library the CPU extractor links, which is the
subject of `T-ICX-LIBIMF-HOST-MATH-2026-10-01` and, for the CUDA twin, of
[ADR-1430](1430-cuda-speed-chroma-log2f-bound.md). `ciede` stays a libm twin
with its derived bound.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Declare by construction and measurement (this ADR) | A listed twin stays listed on input nobody measured; the gate catches a last-bit regression of six twins | `speed_chroma` stays under 5e-5 although it measured identical | Chosen |
| Declare every twin the sweep found identical | Nine features at once | `speed_chroma` would be exact only for one math library; an exact cell that fails on another build is worse than a tolerance | The library dependence is known |
| Leave the tolerances, add nothing | No change | A regression of 1e-6 in an integer twin passes the gate | The measurement exists; not recording it wastes it |
| Tighten each twin's own parity test to `==` instead of one new test | No new file | Six files edited, each with its own fixture and helper shapes | One table-driven test states the contract once, as `test_hip_exact_twins` does |

## Consequences

- **Positive**: the gate compares eight more SYCL cells exactly. With the
  eleven declared before, every SYCL gate feature except `ciede` and
  `speed_chroma` is compared with tolerance 0.
- **Positive**: `float_ssim_sycl` at `scale=1` on 3840x2160 is identical to
  the CPU on 50 BBB frames on the A380. The 8.3e-5 that
  `T-SYCL-FLOAT-SSIM-XE2-XELP-CALIBRATION-2026-09-29` recorded on an Arc B580
  and a UHD 770 predates #1645, which replaced the combined SSIM formula and
  the fp32 reduction with the CPU's arithmetic; the row is closed on that
  evidence, with the B580 and the UHD 770 not re-run.
- **Negative**: `float_ssim` is exact up to one rounding, on the terms of
  `float_ms_ssim` in ADR-1414 and ADR-1437: the frame sum is an exact integer
  sum of the twin's terms, the terms follow the CPU's fp64 values as exact
  fp32 pairs, and the `float` rounding of the mean absorbs what is left
  unless the sum lies within its own error of a rounding boundary. If a mean
  ever differs, the twin has to change; the listing does not get a
  tolerance.
- **Negative**: `cambi` is exact while `cambi.c`'s own top-K sum in `double`
  is exact, which ADR-1357 guarantees below 2^29 per scale and observed well
  beyond; on content past it the CPU carries its accumulation error (a few
  units in the last place) and the cell would fail. None of the 333 frames
  reaches it.
- **Neutral / follow-ups**:
  - The Arc A380 rows of `scripts/ci/gpu_ulp_calibration.yaml` keep
    `float_ssim: 5.0e-4` for cells whose other side is not exact (a
    `cuda`-`sycl` cell); the `cpu`-`sycl` cell no longer reads it.
  - `speed_chroma` and `speed_temporal` on SYCL wait for the host-libm
    question (`T-ICX-LIBIMF-HOST-MATH-2026-10-01`).
  - The options of these twins were not swept here;
    `test_sycl_twin_option_parity` holds them.

## References

- `req` (coordinator brief for the SYCL exactness lane, 2026-10-01): "Then measure every other SYCL twin against the CPU at --precision max on the A380 (Netflix pair, both 1080p checkerboards, testdata/bbb 4K), list which are bit-identical and which are not with the max abs diff, and fix the non-identical ones one PR each in order of the largest difference, isolating the cause per term (types, rounding points, libm calls, reduction order)."
- `req` (rules common to the RC3 lanes, 2026-10-01): "Declaring a twin exact: one fragment file `scripts/ci/exact_twins.d/<feature>.<backend>` (PR #1745, ADR-1428; read an existing fragment for the fields)"
- [ADR-1437](1437-hip-exact-twins-declared.md) (the rule and the HIP
  counterpart), [ADR-1397](1397-psnr-hvs-twins-cpu-float-sum.md) (the exact
  cell), [ADR-1449](1449-sycl-float-moment-cpu-float-squares.md),
  [ADR-1450](1450-sycl-float-psnr-exact-block-sums.md),
  [ADR-0214](0214-gpu-parity-ci-gate.md).
- `docs/state.md`: `T-SYCL-EXACT-TWINS-UNDECLARED-2026-10-02`,
  `T-SYCL-FLOAT-SSIM-XE2-XELP-CALIBRATION-2026-09-29`.
