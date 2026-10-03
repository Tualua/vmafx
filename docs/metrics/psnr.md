<!-- markdownlint-disable MD013 MD060 -->
# PSNR

Peak Signal-to-Noise Ratio: the log-ratio of the maximum possible sample
value to the mean squared error between the reference and the distorted
frame. Higher is better; identical frames are `+inf`.

Two extractors ship it. `psnr` is the fixed-point (integer-accumulated)
path and is what VMAF model JSON files and the CLI's `--feature psnr`
select. `float_psnr` converts both planes to `float` first and is kept for
parity with upstream consumers of the float pipeline; it emits a luma-only
score.

## Variants

| Extractor name | Backend | Feature names | Source |
|---|---|---|---|
| `psnr` | CPU (+ AVX2 / AVX-512 / NEON) | `psnr_y`, `psnr_cb`, `psnr_cr` | `core/src/feature/integer_psnr.c` |
| `psnr_cuda` | CUDA | same | `core/src/feature/cuda/integer_psnr_cuda.c` |
| `psnr_sycl` | SYCL | same | `core/src/feature/sycl/integer_psnr_sycl.cpp` |
| `psnr_hip` | HIP | same | `core/src/feature/hip/integer_psnr_hip.c` |
| `integer_psnr_metal` | Metal | same | `core/src/feature/metal/integer_psnr_metal.mm` |
| `float_psnr` | CPU (+ AVX2 / AVX-512 / NEON) | `float_psnr` | `core/src/feature/float_psnr.c` |
| `float_psnr_cuda` / `_sycl` / `_hip` / `_metal` | CUDA / SYCL / HIP / Metal | `float_psnr` | `core/src/feature/{cuda,sycl,hip,metal}/float_psnr_*` |

GPU twins are selected automatically when the corresponding `--backend` is
active. They emit the same feature names as the CPU extractor, so a model
JSON referencing `psnr` works unchanged. GPU scores are *not* bit-identical
to the CPU reference — see [Backends](../backends/index.md).

## How the score is computed

For each plane `p`:

```text
sse_p = sum over samples of (ref - dis)^2
mse_p = sse_p / (w_p * h_p)
psnr_p = 10 * log10(peak^2 / mse_p)
```

`peak` is `(1 << bpc) - 1` for the integer extractor (255 at 8 bpc, 1023 at
10 bpc, …). `float_psnr` normalises high bit depths back onto an 8-bit
scale and uses `peak` = 255.0 / 255.75 / 255.9375 / 255.99609375 for
8 / 10 / 12 / 16 bpc.

### The `psnr_max` ceiling and the `uncapped` option

When the two planes are byte-identical the SSE is zero and the true PSNR is
`+inf`, which no output schema can carry. Both extractors therefore report a
finite stand-in, `psnr_max`:

| Bit depth | `psnr` (`6 × bpc + 12`) | `float_psnr` |
|---|---|---|
| 8 | 60 dB | 60 dB |
| 10 | 72 dB | 72 dB |
| 12 | 84 dB | 84 dB |
| 16 | 108 dB | 108 dB |

Historically the same ceiling was *also* applied to every genuinely computed
value, so any frame whose true PSNR exceeded it was silently reported as the
ceiling. A 576x324 8-bit pair differing by a single luma step (SSE 1 over
186624 samples, true PSNR 100.840479 dB) reported `psnr_y = 60.000000`.

Since [ADR-1193](../adr/1193-psnr-uncapped-option.md) the two roles are
separate. The `uncapped` option (bool, default `false`) drops the truncation
and keeps the sentinel:

| Case | default | `uncapped=true` |
|---|---|---|
| `mse == 0` (identical planes) | `psnr_max` | `psnr_max` |
| `mse > 0`, true PSNR below `psnr_max` | true value | true value |
| `mse > 0`, true PSNR above `psnr_max` | `psnr_max` (**truncated**) | true value |

The default is unchanged and bit-identical to previous releases, so
`uncapped` never moves an existing score unless you ask for it. The option
exists on `psnr` and `float_psnr` and on all eight GPU twins under the same
name and default. It does not change any feature name.

```bash
# Truncated at the 60 dB ceiling (the default, unchanged behaviour)
core/build/tools/vmaf \
    --reference ref.yuv --distorted dist.yuv \
    --width 576 --height 324 --pixel_format 420 --bitdepth 8 \
    --no_prediction --feature psnr --feature float_psnr --output /dev/stdout
#   "psnr_y": 60.000000   "float_psnr": 60.000000

# True value reported; identical chroma planes still report the 60 dB sentinel
core/build/tools/vmaf \
    --reference ref.yuv --distorted dist.yuv \
    --width 576 --height 324 --pixel_format 420 --bitdepth 8 \
    --no_prediction --feature psnr=uncapped=true \
    --feature float_psnr=uncapped=true --output /dev/stdout
#   "psnr_y": 100.840479  "psnr_cb": 60.000000  "float_psnr": 100.840479
```

Use `uncapped=true` whenever you compare against another PSNR implementation
(FFmpeg's `psnr` filter reports 100.840479 on that same pair), or whenever
you score near-lossless encodes where clipping at 60 dB destroys the ranking
between candidates. Leave it off when you need scores comparable with
previously published VMAF/PSNR numbers, and note that VMAF model JSON files
consume the *capped* feature — turning it on changes what a model that
includes a PSNR term sees.

### `min_sse` — the older escape hatch

`min_sse` (double, default `0.0`) constrains the minimum MSE, which raises
`psnr_max` to `ceil(10 * log10(peak^2 / (min_sse / n_samples)))`. It also
lifts the score of *identical* planes, because it moves the sentinel rather
than removing the truncation: on the pair above,
`--feature psnr=min_sse=0.000001` gives `psnr_y = 100.840479` but reports
`psnr_cb = 155.000000` for byte-identical chroma. Prefer `uncapped` unless
you specifically want a raised sentinel. `min_sse` belongs to the integer
`psnr` extractor only (`float_psnr` has no such option); of its GPU twins,
`psnr_sycl` and `psnr_hip` implement it.

## Options

### `psnr`

| Option | Type | Default | Effect |
|---|---|---|---|
| `enable_chroma` | bool | `true` | Emit `psnr_cb` / `psnr_cr` as well as `psnr_y`. Forced `false` for YUV400P. |
| `enable_mse` | bool | `false` | Also emit `mse_y` / `mse_cb` / `mse_cr`. |
| `enable_apsnr` | bool | `false` | Also emit the clip-aggregate `apsnr_y/cb/cr` at flush. |
| `reduced_hbd_peak` | bool | `false` | Use `255 << (bpc - 8)` as the peak, matching HBD content that was scaled up from 8-bit. |
| `min_sse` | double | `0.0` | Constrain the minimum MSE, raising both the ceiling and the identical-plane sentinel. |
| `uncapped` | bool | `false` | Report the true PSNR instead of truncating at `psnr_max`. The `mse == 0` sentinel is unaffected. |

`psnr_sycl` implements the whole table and matches `--backend cpu` bit for
bit with every option set: the device only reduces each plane's sum of
squared errors, and the host turns it into `psnr_*`, `mse_*` and `apsnr_*`
with the same helpers the CPU extractor uses
(`core/src/feature/psnr_score.h`). `psnr_cuda` does the same since
2026-09-30 ([ADR-1373](../adr/1373-cuda-twin-cpu-option-parity.md)), and like
the CPU `psnr` it sees every frame under `--subsample`, so `apsnr_*` covers
the whole clip (`psnr_sycl` does not yet). `psnr_hip` implements the whole
table since 2026-09-30 as well, also over every frame under `--subsample`
([ADR-1382](../adr/1382-hip-twin-cpu-option-parity.md)); on a gfx1036 its
`psnr_*`, `mse_*` and `apsnr_*` equal the CPU's on the Netflix 576x324 pair
with every option set. The Metal twin implements `enable_chroma` and
`uncapped` only. On that backend a model that sets `enable_mse`, `enable_apsnr`,
`reduced_hbd_peak` or `min_sse` computes `psnr` on the CPU instead
([ADR-1183](../adr/1183-model-options-gate-gpu-twin-selection.md)), and naming
the twin with one of these options fails with `unknown option`.

From the CLI, `--backend sycl` (or `cuda`) runs `--feature psnr` with any of
these options on `psnr_sycl` (or `psnr_cuda`)
([ADR-1359](../adr/1359-cli-feature-backend-twin.md)); naming the twin,
`--feature psnr_sycl=...`, does the same:

```bash
vmaf --reference ref.yuv --distorted dist.yuv \
    --width 576 --height 324 --pixel_format 420 --bitdepth 8 \
    --backend sycl --no_prediction --json --output out.json \
    --feature psnr=enable_mse=true:enable_apsnr=true:min_sse=0.5
```

`mse_y` / `mse_cb` / `mse_cr` then appear per frame and `apsnr_y` /
`apsnr_cb` / `apsnr_cr` under `aggregate_metrics`.

### `float_psnr`

| Option | Type | Default | Effect |
|---|---|---|---|
| `uncapped` | bool | `false` | As above. |

`float_psnr` is luma-only on every backend.

`float_psnr_hip` returns the CPU's score bit for bit at 8, 10, 12 and 16 bits
([ADR-1440](../adr/1440-hip-float-psnr-exact-block-sums.md)): it adds the
squared differences as integers, so its sum is exact, as the CPU's `double`
sum is. Measured on a gfx1036, 178 of 178 frames from 480x270 to 3840x2160
are identical at `--precision max`. Before 2026-10-01 the twin added in
single precision, which matched the CPU on real clips and was up to 7.6e-8 dB
off on high-bit-depth input whose differences are large (full-range noise).
At 16 bits the CPU's own sum rounds once the mean squared error passes
2^37 / (width x height) on the 8-bit scale (16570 at 3840x2160, a PSNR below
6 dB); the twins return its bits there too (see below).

`float_psnr_sycl` does the same since
[ADR-1450](../adr/1450-sycl-float-psnr-exact-block-sums.md): on an Arc A380,
288 of 288 frames from 576x324 to 3840x2160 at 8 to 16 bits are identical at
`--precision max` (269 before; up to 7.4e-8 dB off on full-range
high-bit-depth content).

So does `float_psnr_cuda` since
[ADR-1455](../adr/1455-cuda-float-psnr-exact-block-sums.md). Measured on an
RTX 4090 at `--precision max` against `--backend cpu`, frames identical and
the largest difference:

| Fixture | Before | Now |
|---|---|---|
| Netflix 576x324 at 8 to 16 bits and 4:2:2, both 1080p checkerboards, Sparks 10 bit, BBB 3840x2160, noise at 8 bits | 167 of 167 | 167 of 167 |
| Full-range noise 576x324 at 10, 12 and 16 bits, 3 frames each | 0 of 9, 2.5e-8 dB | 9 of 9 |
| Bright 16-bit 1920x1080 (samples 56000 to 64000), 2 frames | 0 of 2, 7.6e-8 dB | 2 of 2 |
| BBB 1920x1080 widened to 16 bits, 40 frames | 1 of 40, 4.0e-8 dB | 40 of 40 |
| BBB 3840x2160 widened to 16 bits, 32 frames | 0 of 32, 4.0e-8 dB | 32 of 32 |
| Noise at 40x40, 56x56 and 64x64, 8 and 10 bits | 10 of 18, 1.2e-7 dB | 18 of 18 |

The same holds with `uncapped=true`. The frame time is unchanged. Per frame
through the `vmaf` tool, steady state (the time of 52, 40 or 32 frames less
the time of 4, per added frame), medians of 15 interleaved pairs of runs at a
load average of 7 to 10:

| Input | Before | After | Paired difference |
|---|---|---|---|
| 3840x2160, 8 bit | 1.91 ms | 1.90 ms | -0.15 ms |
| 1920x1080, 16 bit | 1.02 ms | 0.97 ms | +0.01 ms |
| 3840x2160, 16 bit | 4.64 ms | 4.37 ms | -0.24 ms |

#### Past 2^53 units (2026-10-03)

The CPU adds each row's squared differences, which is exact, and the rows
into one `double`, which rounds once the sum passes 2^53 units of
1 / scaler^2: a 16-bit frame whose mean squared error times its pixel count
passes 2^37 on the 8-bit scale. Since
[ADR-1499](../adr/1499-float-psnr-twins-cpu-row-order.md) the CUDA, SYCL and
HIP twins return the CPU's bits there too: each block or work-group of the
kernel is a segment of one row, and the host adds each row's exact sum into a
`double` in the CPU's order (`core/src/feature/float_psnr_rows.h`). Before,
they added every block of the frame into one exact total and rounded it once.
Measured at `--precision max` against `--backend cpu`, frames identical, on
an RTX 4090, an Arc A380 and a gfx1036 alike:

| Fixture | Before | Now |
|---|---|---|
| 16-bit 3840x2160 noise, reference in the upper half of the range and distorted frame in the lower half, 8 frames | 0 of 8 (CUDA, 6.2e-15 dB; HIP, 3.3e-13 dB), 1 of 8 (SYCL) | 8 of 8 |
| 16-bit 3840x2160 noise, 16 frames; BBB 3840x2160 widened to 16 bits three ways, 32 frames each | identical | identical |

The frame time is unchanged (4.12 and 4.12 ms per 16-bit 3840x2160 frame on
the RTX 4090, 7.16 and 7.06 ms on the A380, 10.9 and 9.3 ms on the gfx1036,
before and after, pictures preloaded, medians of 5 interleaved runs).

## Output

**Metrics** — `psnr_y`, `psnr_cb`, `psnr_cr` (fixed); `float_psnr` (float).
Plus `mse_*` and `apsnr_*` when the corresponding option is on.

**Range** — dB. Lower bound is unbounded in principle (a fully inverted
frame at 8 bpc gives ~0 dB); upper bound is `psnr_max` unless `uncapped` is
set, in which case only the `mse == 0` case reports `psnr_max`.

**Input formats** — YUV 4:2:0 / 4:2:2 / 4:4:4 / 4:0:0 at 8 / 10 / 12 / 16 bpc.

## Notes and limitations

- The `psnr` extractor sets the temporal flag only because `apsnr`
  accumulates across the clip; the per-frame PSNR itself is stateless.
- `apsnr_*` has its own ceiling, `ceil(10 * log10(peak^2 * n_pixels))`,
  which is a true theoretical maximum rather than a truncation and is not
  affected by `uncapped`.
- A PSNR gap that looks like "28 dB where I expected 72 dB" is almost never
  this ceiling — a `MIN` can only lower a value. Check frame alignment in
  the decode graph first.

## See also

- [Feature overview](features.md) — the full extractor table.
- [ADR-1193](../adr/1193-psnr-uncapped-option.md) — why `uncapped` is opt-in.
- [PSNR-HVS](psnr-hvs.md) — the perceptually weighted variant.
