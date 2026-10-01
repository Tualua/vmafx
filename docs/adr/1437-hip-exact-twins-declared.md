<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1437: `motion_hip`, `motion_v2_hip`, `psnr_hip`, `integer_ms_ssim_hip` and `cambi_hip` are declared exact twins; `float_psnr_hip` and `float_moment_hip` are not

- **Status**: Accepted
- **Date**: 2026-10-01
- **Deciders**: lusoris
- **Tags**: `hip`, `gpu-parity`, `numerics`, `testing`, `ci`, `rc3`, `fork-local`

## Context

RC3 asks every GPU twin to return the CPU extractor's bits
([ADR-1421](1421-rc3-rc8-candidate-map.md)). On HIP three twins were declared
exact (`adm`, `float_motion`, `psnr_hvs`); every other HIP cell of the parity
gate was compared with a tolerance of 5e-5 or looser, whatever the twin
actually returned. A twin that is bit-identical and compared with 5e-5 can
lose its last eleven digits without a gate noticing.

A sweep of all 20 gate features on a gfx1036
([Research-1437](../research/1437-hip-twin-exactness-sweep.md)) compared
every output of every HIP twin with the CPU at `--precision max` on 178
frames: the Netflix pair at 8, 10, 12 and 16 bits and as 4:2:2, both 1080p
checkerboard pairs, Sparks at 10 bits, 48 frames of BBB 3840x2160,
full-range noise at four depths and a bright 16-bit 1080p pair. Nine gate
features were identical on every frame and undeclared: `motion`,
`motion_debug`, `motion_v2`, `psnr`, `float_ms_ssim`, `float_ms_ssim_lcs`,
`cambi`, and on typical content also `float_psnr` and `float_moment`.

Identical on the fixtures is not the same as exact. The noise and 16-bit
frames separated the two:

- `float_psnr_hip` adds each 16x16 block in fp32. That is exact at 8 bits and
  rounds at higher depths once a block's differences are large: up to 7.6e-8
  dB off on the stress frames.
- `float_moment_hip` adds exact integer squares where `moment.c` rounds each
  square to `float`. Equal up to 12 bits, up to 1.0e-4 off in the second
  moments at 16.

## Decision

We will declare a HIP twin exact when it reaches the CPU's value by
construction and the sweep measured it identical on every frame, and list no
twin on measurement alone. That admits five twins, seven gate features:

| Gate feature | Twin | Why its value is the CPU's |
|---|---|---|
| `motion`, `motion_debug` | `motion_hip` | Integer SAD of the differenced, filtered frame on the device; the host weights, caps and blends through the CPU's helpers ([ADR-1377](1377-hip-motion-diff-first.md), [ADR-1382](1382-hip-twin-cpu-option-parity.md)) |
| `motion_v2` | `motion_v2_hip` | The same kernel and launcher |
| `psnr` | `psnr_hip` | Integer SSE in `uint64` per plane; the host concludes through `psnr_score.h`, the CPU's helpers (ADR-1382) |
| `float_ms_ssim`, `float_ms_ssim_lcs` | `integer_ms_ssim_hip` | The CPU's arithmetic type for type and fp32 per-scale means ([ADR-1403](1403-cuda-strict-fp-every-kernel.md)) |
| `cambi` | `cambi_hip` | Integer pipeline with an exact fixed-point top-K sum; the host combine is `cambi.c`'s helpers ([ADR-1378](1378-hip-cambi-device-resident.md)) |

One fragment file per feature under `scripts/ci/exact_twins.d/`
([ADR-1428](1428-exact-twins-fragments.md)) lists `hip` for these features,
so the parity gate compares each cell with tolerance 0 at `--precision max`.
`test_hip_exact_twins` holds all five to `==` on a device.

`float_psnr_hip` and `float_moment_hip` stay under their tolerance until they
are fixed; each has the cause and the fix recorded.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Declare by construction and measurement (this ADR) | A listed twin stays listed on input nobody measured; the gate catches a last-bit regression of five twins | Two twins that are identical on every real clip measured stay under 5e-5 until fixed | Chosen |
| Declare every twin the sweep found identical on the six standard fixtures | Nine features at once | `float_psnr_hip` and `float_moment_hip` would be listed and wrong on 10- to 16-bit input with large differences; an exact cell that fails on some content is worse than a tolerance | The stress frames show they are not exact |
| Leave the tolerances, add nothing | No change | A regression of 1e-6 in an integer twin passes the gate | The measurement exists; not recording it wastes it |
| Tighten each twin's own parity test to `==` instead of one new test | No new file | Seven files edited, each with its own fixture and helper shapes; the motion tests have one frame pair | One table-driven test states the contract once |

## Consequences

- **Positive**: the gate compares seven more HIP cells exactly. On the
  Netflix pair it reports `max_abs_diff=0.000e+00` at tolerance 0 for all of
  them, and for the three declared before.
- **Positive**: the same twins are identical with their options: the motion
  blend, weight, cap, `motion_force_zero` and `motion_moving_average`
  options, `psnr` with `enable_mse`, `enable_apsnr`, `min_sse`,
  `reduced_hbd_peak` and `uncapped`, `float_ms_ssim` with `enable_db` and
  `clip_db`, and `cambi` with window, top-K, threshold, contrast, encode-size
  and EOTF options (3876 values on four fixtures).
- **Negative**: `float_ms_ssim` is exact up to one rounding. The CPU adds the
  per-window terms of a scale into a double in raster order and returns the
  mean as a `float`; the twin adds them in another order. The `float`
  rounding absorbs the order unless the sum lies within its own rounding
  error of a rounding boundary, by estimate a few means in a million. The
  SYCL twin is listed on the same terms (ADR-1414), and the CPU's AVX2 and
  AVX-512 accumulators rely on the same rounding. If a mean ever differs,
  the twin has to add in raster order; the listing does not get a tolerance.
- **Neutral / follow-ups**:
  - Opened from the sweep: `T-HIP-FLOAT-MOMENT-16BIT-SQUARES-2026-10-01`,
    `T-HIP-FLOAT-SSIM-NOT-CPU-ARITHMETIC-2026-10-01`,
    `T-HIP-FLOAT-ADM-NOT-CPU-ARITHMETIC-2026-10-01` and
    `T-HIP-SSIMULACRA2-NOT-CPU-BITS-2026-10-01`. `float_psnr_hip` is fixed on
    its own branch, which carries its row
    (`T-HIP-FLOAT-PSNR-FP32-BLOCK-SUMS-2026-10-01`). `integer_ssim`, `ciede`
    and `float_vif` on HIP were already covered by rows; their first HIP
    numbers are in Research-1437.
  - `integer_ms_ssim_hip` accepts `enable_chroma` and scores luma only
    (`T-MS-SSIM-GPU-CHROMA-OPTION-DRIFT-2026-09-06`); the declaration covers
    the outputs the twin emits.

## References

- `req` (maintainer brief for the second HIP lane, 2026-10-01): "Already identical on all inputs: declare it exact (fragment or literal per common.md) with a test that pins the bits, one PR for the whole group of such twins, state rows updated."
- [Research-1437](../research/1437-hip-twin-exactness-sweep.md) (the sweep).
- [ADR-1397](1397-psnr-hvs-twins-cpu-float-sum.md) (the exact cell),
  [ADR-1414](1414-sycl-float-ms-ssim-cpu-arithmetic.md),
  [ADR-1423](1423-hip-adm-cpu-row-rounding.md),
  [ADR-1419](1419-hip-float-motion-cpu-float-sum.md),
  [ADR-1401](1401-psnr-hvs-sycl-hip-exact-twins.md),
  [ADR-0214](0214-gpu-parity-ci-gate.md).
