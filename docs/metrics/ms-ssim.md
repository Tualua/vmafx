<!-- markdownlint-disable MD013 MD060 -->
# MS-SSIM

MS-SSIM (Multi-Scale Structural Similarity Index Measure) extends SSIM to a
multi-resolution pyramid, providing a perceptual similarity metric that is
robust to viewing distance and display resolution variation. Each scale
captures structural information at a different spatial frequency.

## Variant

The fork ships one CPU MS-SSIM extractor:

| Extractor name | Algorithm | Options |
|---|---|---|
| `float_ms_ssim` | Floating-point IQA library, 5-scale Gaussian pyramid | `enable_lcs`, `enable_db`, `clip_db`, `enable_chroma` |

Every GPU twin takes the CPU extractor's four options and computes what they
ask for on the device. Read from the twins' own `options[]` tables:

| Twin | `enable_lcs` | `enable_db` | `clip_db` | `enable_chroma` |
|---|---|---|---|---|
| `float_ms_ssim_cuda` | yes | yes | yes | yes — computed on the GPU (3 planes), since 2026-10-03 |
| `float_ms_ssim_sycl` | yes | yes | yes | yes — computed on the GPU (3 planes) |
| `integer_ms_ssim_hip` | yes | yes | yes | yes — computed on the GPU (3 planes), since 2026-10-03 |
| `float_ms_ssim_metal` | yes | yes | yes | yes — computed on the GPU (3 planes) |

With `enable_chroma=true` each twin gives every scored plane its own
geometry, pyramid and score storage, runs the luma pipeline on it and writes
`float_ms_ssim_cb` and `float_ms_ssim_cr`, which every twin lists among the
features it provides, so a model that asks for them is served on the GPU.
On CUDA, SYCL and HIP the three scores are the CPU extractor's bit for bit
(measured below). The Metal twin got the option with
[ADR-1334](../adr/1334-metal-ms-ssim-option-parity.md), and the SYCL twin
with [ADR-1299](../adr/1299-sycl-ms-ssim-chroma-implementation.md).

> **HIP runs with `enable_chroma` before 2026-10-03 have no chroma scores.**
> `integer_ms_ssim_hip` accepted the option, scored luma only and wrote
> neither `float_ms_ssim_cb` nor `float_ms_ssim_cr`, without a warning. The
> CUDA twin had no such option, so the same request ran the CPU extractor
> and its scores were right. Re-run any HIP measurement that needs the chroma
> scores.

### Minimum resolution with chroma

The 5-level 11-tap pyramid needs every scored plane to be at least 176x176, and
with `enable_chroma` that includes the subsampled ones. Plane allocation uses
ceil subsampling, so for 4:2:0 the exact luma minimum is 351x351 (352x352 is
the next even-sized input), not 176x176; for 4:2:2 it is 351x176. The CPU
extractor and every twin refuse an input whose scored plane is smaller at
init and name the chroma size they measured, so a run on this repository's
576x324 4:2:0 Netflix fixture (288x162 chroma) with `enable_chroma` writes no
scores on any backend. YUV400P has no chroma: the option is ignored and luma
is scored. Before ADR-1299 the CPU extractor checked luma only and a 4:2:0
input in between died mid-run with `error: scale below 1x1!` on stdout and no
output file.

### Selecting the twin from the command line

`--backend cuda`, `--backend sycl` or `--backend hip` with
`--feature float_ms_ssim=enable_chroma=true` runs that backend's twin
(the JSON output's `feature_backends` names it); `--backend cpu` runs the
CPU extractor. A model that names the features reaches the twin the same
way. (The Vulkan backend was removed in ADR-0726.)

```bash
vmaf --reference ref.yuv --distorted dist.yuv \
     --width 1920 --height 1080 --pixel_format 420 --bitdepth 8 \
     --backend cuda --feature float_ms_ssim=enable_chroma=true \
     --no_prediction --json --output ms_ssim.json --precision max
```

Tracked as `T-MS-SSIM-GPU-CHROMA-OPTION-DRIFT-2026-09-06` in
[docs/state.md](../state.md).

## `float_ms_ssim` extractor

The extractor uses the IQA library's Gaussian-window floating-point
implementation with a 5-scale Laplacian pyramid (Wang et al. 2004). It is the
extractor invoked when VMAF model JSON files reference `"float_ms_ssim"`.

The minimum supported input resolution is 176x176. Smaller inputs cause the
5-level pyramid to fall below the 11-tap Gaussian kernel footprint and are
rejected with an error at init time (Netflix#1414 / ADR-0153).

### Output features

| Feature name | Description | Condition |
|---|---|---|
| `float_ms_ssim` | MS-SSIM on the luma (Y) plane | Always |
| `float_ms_ssim_cb` | MS-SSIM on the Cb (U) chroma plane | `enable_chroma=true` only |
| `float_ms_ssim_cr` | MS-SSIM on the Cr (V) chroma plane | `enable_chroma=true` only |
| `float_ms_ssim_l_scale0-4` | Per-scale luminance component | `enable_lcs=true`, luma only |
| `float_ms_ssim_c_scale0-4` | Per-scale contrast component | `enable_lcs=true`, luma only |
| `float_ms_ssim_s_scale0-4` | Per-scale structure component | `enable_lcs=true`, luma only |

## Options

- `enable_chroma` (bool, default `false`): emit per-plane `_cb` and `_cr` scores in addition to luma. YUV400P sources are always luma-only.
- `enable_lcs` (bool, default `false`): emit per-scale luminance, contrast, and structure intermediate components for the luma plane.
- `enable_db` (bool, default `false`): report the luma MS-SSIM score as dB (`-10 * log10(1 - score)`).
- `clip_db` (bool, default `false`): cap the dB score at a **ceiling derived
  from the frame geometry** — `max_db = ceil(10 * log10(peak² / mse))` with
  `mse = 0.5 / (w * h)`. It is a ceiling on the dB *output*, not a clamp on the
  linear score, and it also defines what a perfect match reports: `score >= 1.0`
  returns `max_db` rather than `+Inf`. Only meaningful when `enable_db=true`.

  > **GPU scores before this release were wrong.** Up to and including v3.2.1 the
  > CUDA, SYCL and HIP twins read `clip_db` as a clamp on the *linear* score —
  > `[0, 1]`, then `-10 * log10(1 - score)` with no ceiling — and carried no
  > `max_db` at all. Scoring an identical reference/distorted pair returned
  > `+Inf`, and every high-similarity pair returned an uncapped dB value, so
  > `clip_db` did not clip. Fixed per
  > [ADR-1221](../adr/1221-gpu-ms-ssim-db-ceiling.md). **Re-measure any GPU
  > MS-SSIM dB score taken with `clip_db` set.** The Metal twin has been brought
  > to full parity by [ADR-1334](../adr/1334-metal-ms-ssim-option-parity.md),
  > implementing `enable_db`, `clip_db` with `max_db` ceiling, and
  > `enable_chroma`.

### How to run

```bash
# Luma-only MS-SSIM (default)
core/build/tools/vmaf \
    --reference ref.yuv --distorted dist.yuv \
    --width 1920 --height 1080 --pixel_format 420 --bitdepth 8 \
    --no_prediction --feature float_ms_ssim --output /dev/stdout

# Per-channel MS-SSIM (luma + Cb + Cr)
core/build/tools/vmaf \
    --reference ref.yuv --distorted dist.yuv \
    --width 1920 --height 1080 --pixel_format 420 --bitdepth 8 \
    --no_prediction --feature float_ms_ssim=enable_chroma=true --output /dev/stdout
```

## Precision of the GPU twins

`float_ms_ssim_cuda` and `float_ms_ssim_sycl` compute the CPU extractor's
arithmetic: the decimate fuses each tap as `ms_ssim_decimate.c` does, the
Gaussian window sums are fp32 products added as the CPU's `double` sum is,
and the luminance, contrast and structure terms use the CPU's operand types.
The CUDA twin does so in `double`
([ADR-1403](../adr/1403-cuda-strict-fp-every-kernel.md)); the SYCL twin, which
may not use `double` on the device, carries those values as exact pairs of
floats and adds the frame sums in 64-bit fixed point
([ADR-1414](../adr/1414-sycl-float-ms-ssim-cpu-arithmetic.md)).

Measured on an Arc A380 at `--precision max` against `--backend cpu`,
`float_ms_ssim_sycl`, frames identical and largest difference:

| Fixture | Before | After |
|---|---|---|
| Netflix 576x324, 48 frames | 0 of 48, 6.9e-8 | 48 of 48 |
| Checkerboard 1920x1080, 1 px shift, 3 frames | 0 of 3, 1.06e-6 | 3 of 3 |
| Checkerboard 1920x1080, 10 px shift, 3 frames | 0 of 3, 2.98e-6 | 3 of 3 |
| BBB 3840x2160 | 0 of 50, 1.23e-6 | 199 of 200, 1.1e-16 |

With `enable_lcs` all 15 per-scale means are the CPU's on every frame (50
BBB frames), and so are `float_ms_ssim_cb` / `float_ms_ssim_cr` with
`enable_chroma`, and the score at 10, 12 and 16 bits. The reference in this
table is a GCC build. The one BBB frame that differs does so in the last bit
of the `double`: the twin's host combine calls the `pow()` of its own
Intel-compiler build. Against the CPU extractor of its own binary the twin is
identical on every frame. With `enable_db` the same holds for `log10()`
(3.6e-15 on 3 of 48 Netflix frames against a GCC build).

The match after that work was exact in practice, not by construction: the
CPU adds each frame's terms into a running `double`, the twins added them in
another order, and the rounding of each per-scale mean to `float` absorbs the
difference unless the mean sits next to a rounding boundary. Such frames
exist (`T-GPU-FLOAT-SSIM-FRAME-SUM-ORDER-2026-10-02`): on a 176x176 noise
pair `float_ms_ssim_sycl` returned 0.9884905219078064 for
`float_ms_ssim_l_scale0` where the CPU returns 0.9884904623031616, one
`float` step. Since 2026-10-02 the SYCL twin stores every window's `l`, `c`
and `s` of every scale, with `l` and `c` as the CPU's `double` values, and
adds them on the host in the CPU's order
([ADR-1466](../adr/1466-sycl-float-ms-ssim-raster-sum.md)); it is exact by
construction and returns the CPU's value on that pair. Against a GCC build of
the CPU extractor on an Arc A380: `float_ms_ssim` on 138 of 138 frames and
2208 of 2208 `enable_lcs` values. That costs 75.9 ms per 3840x2160 frame
against 44.7 before, 18.2 against 11.5 ms at 1920x1080 and 1.92 against 1.16
ms at 576x324, and 219 MB of device and pinned host memory at 3840x2160
([SYCL backend](../backends/sycl/overview.md#float_ms_ssim_sycl-adds-its-per-scale-sums-in-the-cpus-order-2026-10-02)).

The SYCL twin pays for this in time: 42.6 ms per 3840x2160 frame on the A380
against 31.4 before (0.84 to 1.20 ms at 576x324); the CPU extractor takes 117
ms on the same host. The HIP twin computes the CPU's arithmetic as well
(ADR-1403) and adds its sums in the CPU's order; it is listed as exact in the
parity gate. The Metal twin still uses the old arithmetic and agrees with the
CPU to within 5e-5.

### The chroma planes

With `enable_chroma` the twins run the same kernels and host sums on each
chroma plane, so the chroma scores are exact for the same reasons the luma
score is. Measured on 2026-10-03 at `--precision max` against the CPU
extractor of a GCC build, with each option set (`enable_chroma` alone, with
`enable_lcs`, with `enable_db`, with `enable_db` and `clip_db`):

| Fixture | Frames | CUDA (RTX 4090) | HIP (gfx1036) |
|---|---:|---|---|
| Checkerboard 1920x1080 4:2:0, 1 px and 10 px shift | 3 + 3 | identical | identical |
| Netflix 576x324 4:2:2 10-bit | 48 | identical | identical |
| Netflix 576x324 4:4:4, 8 and 10 bit | 48 + 48 | identical | identical |
| Netflix 576x324 4:4:4, reference against itself | 48 | identical | identical |
| BBB 1920x1080 4:4:4 | 24 | identical | identical |
| BBB 3840x2160 4:2:0 | 30 | identical | identical |
| Netflix 576x324 4:2:0 (288x162 chroma) | 48 | refused, as the CPU | refused, as the CPU |

That is 6 804 of 6 804 values on each of CUDA and HIP, the
`feature_backends` of every run naming the twin. The SYCL twin (Arc A380,
build of `master` 9aa990455) returns the CPU extractor of its own binary on
the same runs, 5 508 of 5 508 values; against the GCC build 52 of 6 804
values differ by at most 2.1e-14, all from the Intel math library's `pow()`
and `log10()` in its host combine. The parity gate's `float_ms_ssim_chroma`
cell compares the three scores with tolerance 0 on CUDA, SYCL and HIP; on a
fixture whose chroma is below 176 pixels it is reported `SKIP` with the
reason.

The chroma planes cost what their area costs: a 4:2:0 frame scores 1.5 times
the luma area, a 4:4:4 frame 3 times. Milliseconds per frame,
(t(N) - t(2)) / (N - 2) through the `vmaf` tool, median of 3:

| Input | CUDA luma | CUDA with chroma | HIP luma | HIP with chroma |
|---|---:|---:|---:|---:|
| BBB 3840x2160 4:2:0, N = 42 | 33.0 | 46.5 | 186.7 | 257.8 |
| BBB 1920x1080 4:4:4, N = 24 | 9.7 | 22.6 | 39.1 | 119.2 |

Device and pinned host memory grow by the same factor: every plane keeps its
own pyramid and per-window terms.

## See also

- [SSIM](ssim.md) - single-scale structural similarity
- [SSIMULACRA2](ssimulacra2.md) - perceptually tuned alternative
