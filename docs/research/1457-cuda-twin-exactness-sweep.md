<!-- markdownlint-disable MD013 MD060 -->
# Research-1457: Which CUDA twins return the CPU extractor's bits — a sweep of every gate feature on an RTX 4090, stress content and small frames included

- **Status**: Active
- **Workstream**: [ADR-1457](../adr/1457-cuda-exact-twins-declared.md), [ADR-1421](../adr/1421-rc3-rc8-candidate-map.md)
- **Last updated**: 2026-10-02

## Question

RC3 asks every GPU twin to return the CPU extractor's bits, or to differ only
by the math library with a derived bound
([ADR-1421](../adr/1421-rc3-rc8-candidate-map.md)). The CUDA twins were made
exact one at a time and measured on the usual fixtures. The HIP and SYCL
lanes then swept their twins on high-bit-depth and full-range content
([Research-1437](1437-hip-twin-exactness-sweep.md), ADR-1451) and each found
twins that only fail there. Which CUDA twins fail on that content, on small
frames, or on outputs the parity gate does not list?

## Sources

- `origin/master` 2096bd1bb ("before") and 906e649ea ("now", after the two
  fixes this sweep led to), CUDA-only release builds (`-Denable_cuda=true
  -Denable_nvcc=true -Denable_sycl=false -Denable_hip=false -Db_lto=false`),
  gcc 16.2.1, glibc 2.44, CUDA 13.4, RTX 4090, Linux 7.2.8-1-cachyos,
  2026-10-02. Other sessions shared the host.
- Every key of `FEATURE_METRICS` in
  `scripts/ci/cross_backend_parity_gate.py` (21 features, all with a CUDA
  twin), run through that gate's command builder with `--precision max`,
  `--backend cpu` against `--backend cuda` with the twin named, and its
  frame diff with tolerance 0 over every output both sides emit, not only the
  outputs the gate lists. An output only one side emits is reported.
- **Typical** fixtures (105 frames): the Netflix 576x324 pair at 8 bits (48
  frames) and 10 bits (3), the two 1920x1080 checkerboard pairs (3 each) and
  the first 48 frames of BBB 3840x2160.
- **Stress** fixtures (73 frames), the set the HIP lane built: the Netflix
  pair at 12 and 16 bits (3 frames each) and as 10-bit 4:2:2 (48),
  independent full-range noise at 576x324 and 8, 10, 12 and 16 bits (3 frames
  each), a bright 16-bit 1920x1080 pair (samples 56000 to 64000, 2 frames)
  and Sparks 480x270 at 10 bits (5).
- **Small** fixtures (18 frames): independent noise at 40x40, 56x56 and 64x64,
  8 and 10 bits (3 frames each). `float_vif_hip` faulted its GPU at these
  sizes.
- Further runs of the twins that were identical everywhere: 200 frames of BBB
  3840x2160, and 21 option sets on five fixtures (59 frames each).
- A cell that differed was run twice more. Every such cell returned the same
  values in all three runs, so each difference below is arithmetic.

## Findings

### Outputs identical to the CPU's, per feature

`identical / compared` over every common output of every frame, and the
largest absolute difference where there is one.

| feature | outputs | typical before | typical now | stress before | stress now | small before | small now |
|---|---:|---|---|---|---|---|---|
| `vif` | 4 | 420/420 | 420/420 | 292/292 | 292/292 | 72/72 | 72/72 |
| `motion` | 2 | 210/210 | 210/210 | 146/146 | 146/146 | 36/36 | 36/36 |
| `motion_debug` | 3 | 315/315 | 315/315 | 219/219 | 219/219 | 54/54 | 54/54 |
| `motion_v2` | 3 | 315/315 | 315/315 | 219/219 | 219/219 | 54/54 | 54/54 |
| `adm` | 7 | 735/735 | 735/735 | 511/511 | 511/511 | 126/126 | 126/126 |
| `psnr` | 3 | 315/315 | 315/315 | 219/219 | 219/219 | 54/54 | 54/54 |
| `float_moment` | 4 | 420/420 | 420/420 | 282/292 (1.0e-04) | 292/292 | 72/72 | 72/72 |
| `ciede` | 1 | 56/105 (1.4e-11) | 56/105 (1.4e-11) | 59/73 (4.6e-12) | 59/73 (4.6e-12) | 18/18 | 18/18 |
| `ssim` | 1 | 105/105 | 105/105 | 73/73 | 73/73 | 18/18 | 18/18 |
| `float_ssim` | 1 | 105/105 | 105/105 | 73/73 | 73/73 | 18/18 | 18/18 |
| `float_ssim_lcs` | 4 | 420/420 | 420/420 | 292/292 | 292/292 | 72/72 | 72/72 |
| `float_ms_ssim` | 1 | 105/105 | 105/105 | 73/73 | 73/73 | - | - |
| `float_ms_ssim_lcs` | 16 | 1680/1680 | 1680/1680 | 1168/1168 | 1168/1168 | - | - |
| `float_psnr` | 1 | 105/105 | 105/105 | 62/73 (7.6e-08) | 73/73 | 10/18 (1.2e-07) | 18/18 |
| `float_motion` | 3 | 315/315 | 315/315 | 219/219 | 219/219 | 54/54 | 54/54 |
| `float_vif` | 4 | 420/420 | 420/420 | 292/292 | 292/292 | 72/72 | 72/72 |
| `psnr_hvs` | 4 | 420/420 | 420/420 | 260/260 | 260/260 | 72/72 | 72/72 |
| `float_adm` | 7 | 735/735 | 735/735 | 511/511 | 511/511 | 126/126 | 126/126 |
| `ssimulacra2` | 1 | 105/105 | 105/105 | 73/73 | 73/73 | 18/18 | 18/18 |
| `cambi` | 1 | 105/105 | 105/105 | 73/73 | 73/73 | - | - |
| `speed_chroma` | 3 | 309/315 (1.4e-06) | 309/315 (1.4e-06) | 219/219 | 219/219 | - | - |

No twin faulted, hung or returned a non-finite value on any fixture. At 40x40
to 64x64 `float_ms_ssim_cuda`, `cambi_cuda` and `speed_chroma_cuda` refuse
the frame with the CPU extractor's message (minimum 176x176, 216 and 80x80
chroma), as the CPU does; `psnr_hvs` refuses 16 bits on both sides.

### What each result is

| Feature | Result | Disposition |
|---|---|---|
| `adm`, `ssim`, `float_adm`, `float_motion`, `float_vif`, `psnr_hvs`, `ssimulacra2` | Identical on every fixture | Declared exact before this sweep; confirmed on the stress and small sets |
| `motion`, `motion_debug`, `motion_v2`, `psnr`, `float_ssim`, `float_ssim_lcs`, `float_ms_ssim`, `float_ms_ssim_lcs`, `cambi` | Identical on every fixture, on 200 BBB frames and under their options | Declared exact by ADR-1457 |
| `vif` | Identical on every fixture | Evaluates `log2f()` on the device; probed on every table entry and declared exact by ADR-1456 (#1810) |
| `float_moment` | Second moments up to 1.0e-4 off on 16-bit content with real low bits | Fixed by [ADR-1453](../adr/1453-cuda-float-moment-cpu-float-squares.md): integer squares where the CPU adds float squares |
| `float_psnr` | Up to 1.2e-7 dB off at 10, 12 and 16 bits on large differences; the only twin the small frames caught | Fixed by [ADR-1455](../adr/1455-cuda-float-psnr-exact-block-sums.md): fp32 block sums |
| `motion`, `motion_debug` | The CPU emits `VMAF_integer_feature_motion_sad_score`; the twin did not | Fixed in #1809 (`T-CUDA-MOTION-SAD-SCORE-NOT-EMITTED-2026-10-02`); the gate lists `motion2` / `motion3` only and could not see it |
| `ciede` | 1.4e-11 at most | The CPU's arithmetic except for the math library; bound 1e-9 ([ADR-1426](../adr/1426-cuda-ciede-cpu-arithmetic.md)) |
| `speed_chroma` | 1.4e-6 at most, on 6 of 315 typical values | The device's `log2` is correctly rounded, glibc's `log2f` is not on every argument; bound 5e-6 ([ADR-1430](../adr/1430-cuda-speed-chroma-log2f-bound.md)) |

### Options

The twins ADR-1457 declares are identical to the CPU under their options
too, on the Netflix pair at 8 and 10 bits, 12-bit noise, a 1080p checkerboard
and the bright 16-bit pair (59 frames, 21 option sets, 3422 values, `vif`'s
three included): `motion` with
`motion_force_zero`, `motion_moving_average`, and weight, blend and cap;
`motion_v2` with the moving average and with weight and cap; `psnr` with
`enable_mse`, `enable_apsnr`, `min_sse`, `reduced_hbd_peak` and
`uncapped`; `float_ssim` with `enable_db`, `clip_db`, `scale=1`,
`scale=3` and `enable_lcs`; `float_ms_ssim` with `enable_db` and
`clip_db`; `cambi` with encode size, EOTF, window, top-K, threshold and
contrast options; `vif` with `debug`, `vif_enhn_gain_limit=1.0` and
`vif_skip_scale0`.

`float_ms_ssim_cuda` rejects `enable_chroma`, which the CPU extractor
accepts (`T-MS-SSIM-GPU-CHROMA-OPTION-DRIFT-2026-09-06`).

### The caveat on `float_ssim` and `float_ms_ssim`

Both CPU extractors add their per-window terms into a `double` in raster
order and return the mean as a `float`. The twins compute the CPU's terms
type for type and add them in `double` per block, then round the mean to
`float` the same way. The two sums differ by their order only, a relative
1e-13 or so, and the `float` rounding absorbs that unless the sum lies
within its own rounding error of a rounding boundary: by estimate a few means
in a million. None of the 9000 means compared here differs. The HIP
and SYCL twins are listed on the same terms (ADR-1437, ADR-1451).

## Method notes

- The gate's own listing was not enough: it lists `motion2` / `motion3`
  for `motion`, and the CPU emits a third output the twin lacked. The sweep
  compares every output both sides emit and reports an output only one side
  has.
- The repository's 10-, 12- and 16-bit Netflix fixtures are the 8-bit clip
  shifted left: every sample has zero low bits. They exercise no arithmetic
  an 8-bit frame does not.
- 8-bit natural content does not separate "exact by construction" from "exact
  on this input". The noise frames and the 16-bit frames did, for
  `float_psnr_cuda` and `float_moment_cuda`, as they had on HIP and SYCL.
- Equal scores do not prove a device math call equals the host's on every
  argument. `vif_cuda`'s `log2f()` was therefore probed over its whole
  domain (ADR-1456) instead of being declared from this table.

## Open questions

- `speed_temporal_cuda` is not a parity-gate feature and was not swept.
- Whether `float_ssim_cuda` and `float_ms_ssim_cuda` should add their
  terms in the CPU's raster order, as `integer_ssim_cuda` does since
  ADR-1424, to remove the caveat above at the cost of a larger readback.
