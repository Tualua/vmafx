<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1457: `motion_cuda`, `motion_v2_cuda`, `psnr_cuda`, `float_ssim_cuda`, `float_ms_ssim_cuda` and `cambi_cuda` are declared exact twins

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `cuda`, `gpu-parity`, `numerics`, `testing`, `ci`, `rc3`, `fork-local`

## Context

RC3 asks every GPU twin to return the CPU extractor's bits
([ADR-1421](1421-rc3-rc8-candidate-map.md)). On CUDA nine gate features were
declared exact, each by the ADR that made its twin so (`adm`, `ssim`,
`float_adm`, `float_moment`, `float_motion`, `float_psnr`, `float_vif`,
`psnr_hvs`, `ssimulacra2`). The other CUDA cells of the parity gate were
compared with a tolerance of 5e-5 or looser, whatever the twin returned. A
twin that is bit-identical and compared with 5e-5 can lose its last eleven
digits without a gate noticing
([ADR-1437](1437-hip-exact-twins-declared.md) and
[ADR-1451](1451-sycl-exact-twins-declared.md) made the same observation for
HIP and SYCL).

A sweep of all 21 gate features on an RTX 4090
([Research-1457](../research/1457-cuda-twin-exactness-sweep.md)) compared
every output of every CUDA twin with the CPU at `--precision max` on 196
frames: the Netflix pair at 8, 10, 12 and 16 bits and as 4:2:2, both 1080p
checkerboard pairs, Sparks at 10 bits, 48 frames of BBB 3840x2160, full-range
noise at four depths, a bright 16-bit 1080p pair, and noise at 40x40, 56x56
and 64x64. It found three defects, each fixed in its own change
(`float_moment_cuda`, [ADR-1453](1453-cuda-float-moment-cpu-float-squares.md);
`float_psnr_cuda`, [ADR-1455](1455-cuda-float-psnr-exact-block-sums.md); a
missing `motion` output). Nine gate features were identical on every frame
and undeclared.

## Decision

We will declare a CUDA twin exact when it reaches the CPU's value by
construction and the sweep measured it identical on every frame, and list no
twin on measurement alone (the rule of ADR-1437). That admits six twins, nine
gate features:

| Gate feature | Twin | Why its value is the CPU's |
|---|---|---|
| `motion`, `motion_debug` | `motion_cuda` | Integer SAD of the differenced, filtered frame with the CPU's rounding per pass; the host weights, caps and blends as `integer_motion.c` does ([ADR-1372](1372-cuda-motion-diff-first-pipeline.md), [ADR-1373](1373-cuda-twin-cpu-option-parity.md)) |
| `motion_v2` | `motion_v2_cuda` | The same kernel and launcher, and the CPU flush's formula (ADR-1372, ADR-1373) |
| `psnr` | `psnr_cuda` | Integer SSE in `uint64` per plane; the host concludes through `psnr_score.h`, the CPU's helpers (ADR-1373) |
| `float_ssim`, `float_ssim_lcs` | `float_ssim_cuda` | The CPU's decimated planes byte for byte, both Gaussian passes added in `double` as `iqa_convolve()` adds them, the per-window `l * c * s` operand for operand, and the frame mean rounded to `float` as `iqa_ssim()` rounds it ([ADR-1399](1399-cuda-float-ssim-device-decimation.md)) |
| `float_ms_ssim`, `float_ms_ssim_lcs` | `float_ms_ssim_cuda` | The CPU's arithmetic type for type and fp32 per-scale means ([ADR-1403](1403-cuda-strict-fp-every-kernel.md)) |
| `cambi` | `cambi_cuda` | Integer pipeline, fp32 c-values that are `cambi.c`'s, an exact fixed-point top-K sum and the CPU's host combine ([ADR-1379](1379-cuda-cambi-device-resident-pipeline.md)) |

One fragment file per feature under `scripts/ci/exact_twins.d/`
([ADR-1428](1428-exact-twins-fragments.md)) lists `cuda` for these features,
so the parity gate compares each cell with tolerance 0 at `--precision max`.
`test_cuda_exact_twins` holds all six to `==` on a device.

`vif` is declared separately (ADR-1456): its twin evaluates a logarithm on
the device, which a table of scores cannot prove equal to the host's.
`ciede` and `speed_chroma` stay libm twins with their derived bounds
([ADR-1426](1426-cuda-ciede-cpu-arithmetic.md),
[ADR-1430](1430-cuda-speed-chroma-log2f-bound.md)); the sweep measured them
at 1.4e-11 and 1.4e-6, inside 1e-9 and 5e-6.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Declare by construction and measurement (this ADR) | A listed twin stays listed on input nobody measured; the gate catches a last-bit regression of six twins | The two SSIM twins are exact up to one `float` rounding, see below | Chosen |
| Declare every twin the sweep found identical, `vif` included, from the table alone | One PR | `vif_cuda` computes `log2f()` on the device, and on another GPU that was wrong on 77 of 32768 arguments while most frames agreed (ADR-1435) | A device math call needs a proof over its domain |
| Make the two SSIM twins add their terms in the CPU's raster order first, as `integer_ssim_cuda` does ([ADR-1424](1424-cuda-ssim-cpu-frame-sum.md)) | Removes the one-rounding caveat | A per-pixel readback and a host sum for twins that differ on none of about 9000 measured means; the HIP and SYCL twins are listed on the same terms | Left as an open question in Research-1457 |
| Leave the tolerances, add nothing | No change | A regression of 1e-6 in an integer twin passes the gate | The measurement exists; not recording it wastes it |
| Tighten each twin's own parity test to `==` instead of one new test | No new file | Six files edited, each with its own fixture and helper shapes | One table-driven test states the contract once, as `test_hip_exact_twins` and `test_sycl_exact_twins` do |

## Consequences

- **Positive**: the gate compares nine more CUDA cells exactly. With the nine
  declared before and `vif` (ADR-1456), every CUDA gate feature except
  `ciede` and `speed_chroma` is compared with tolerance 0.
- **Positive**: the same twins are identical with their options (18 option
  sets, 2065 values on five fixtures) and on 200 frames of BBB 3840x2160; the list is in
  Research-1457.
- **Negative**: `float_ssim` and `float_ms_ssim` are exact up to one
  rounding. The CPU adds the per-window terms into a `double` in raster order
  and returns the mean as a `float`; the twins add the same terms per block.
  The `float` rounding absorbs the order unless the sum lies within its own
  rounding error of a rounding boundary, by estimate a few means in a
  million. If a mean ever differs, the twin has to add in raster order; the
  listing does not get a tolerance.
- **Negative**: `cambi` is exact while `cambi.c`'s own top-K sum in `double`
  is exact, which holds below 2^29 per scale and in practice well beyond
  (ADR-1379); on content past it the CPU carries its accumulation error and
  the cell would fail. None of the measured frames reaches it.
- **Neutral / follow-ups**:
  - `float_ms_ssim_cuda` rejects `enable_chroma`, which the CPU accepts
    (`T-MS-SSIM-GPU-CHROMA-OPTION-DRIFT-2026-09-06`); the declaration covers
    the outputs the twin emits.
  - `speed_temporal_cuda` is not a gate feature and was not swept.
  - Kernels and tests that exist once per backend with the same content are
    recorded for the deduplication phase in
    `T-GPU-CUDA-HIP-DUPLICATED-KERNELS-2026-10-02`.

## References

- `req` (coordinator brief for the CUDA lane, 2026-10-02): "One group PR that declares every twin that is identical on all of it exact (fragments + a test that pins the bits, as HIP's #1772 and SYCL's #1795 did; read both). Twins that are only libm-different get a measured `LIBM_TWINS` bound."
- `req` (same brief): "Shared-code notes for RC5 (dedupe), rows not fixes: the four ssimulacra2 sum kernels exist in both `.cu` and `.hip`; anything else you see duplicated between the CUDA and HIP twins while porting."
- [Research-1457](../research/1457-cuda-twin-exactness-sweep.md) (the sweep).
- [ADR-1437](1437-hip-exact-twins-declared.md),
  [ADR-1451](1451-sycl-exact-twins-declared.md),
  [ADR-1397](1397-psnr-hvs-twins-cpu-float-sum.md) (the exact cell),
  [ADR-0214](0214-gpu-parity-ci-gate.md).
- `docs/state.md`: `T-CUDA-EXACT-TWINS-UNDECLARED-2026-10-02` (opened and
  closed by this decision), `T-GPU-CUDA-HIP-DUPLICATED-KERNELS-2026-10-02`.
