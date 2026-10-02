# SSIMULACRA 2 Extractor

SSIMULACRA 2 is a full-reference perceptual similarity metric from the JPEG XL
ecosystem. It compares reference and distorted frames in an XYB-inspired colour
space, combines multi-scale SSIM-style structural terms, and applies an
asymmetric penalty for lost texture energy. The fork ships it as a normal
libvmaf feature extractor named `ssimulacra2`.

## Output

| Field | Value |
| --- | --- |
| Feature name | `ssimulacra2` |
| Output metric | `ssimulacra2` |
| Direction | Higher is better |
| Range | `[0, 100]`; identical frames return `100` |
| Snapshot gate | `python/test/ssimulacra2_test.py` |

Practical score bands:

| Score | Meaning |
| --- | --- |
| `90-100` | Visually lossless |
| `70-90` | High quality |
| `50-70` | Medium quality, clearly lossy |
| `30-50` | Low quality |
| `0-30` | Very low quality |

## Usage

```bash
vmaf \
    --reference ref.yuv \
    --distorted dist.yuv \
    --width 1920 --height 1080 --pixel_format 420 --bitdepth 8 \
    --feature ssimulacra2 \
    --output score.json
```

The per-frame JSON metric key is `ssimulacra2`; pooled values appear under the
same key in `pooled_metrics`.

## Options

| Option | Type | Default | Values | Effect |
| --- | --- | --- | --- | --- |
| `yuv_matrix` | int | `0` | `0..3` | `0`: BT.709 limited, `1`: BT.601 limited, `2`: BT.709 full, `3`: BT.601 full |

Example:

```bash
vmaf ... --feature ssimulacra2=yuv_matrix=2
```

## Inputs And Backends

- Pixel formats: YUV 4:2:0, 4:2:2, and 4:4:4. The colour conversion needs
  both chroma planes, so 4:0:0 (luma-only) input is refused at init with
  `ssimulacra2: needs a YUV 4:2:0, 4:2:2 or 4:4:4 input, not 4:0:0` and
  `-EINVAL` from `vmaf_read_pictures()`. (The `vmaf` CLI never produces it: it
  rejects `-p 400` and converts Y4M `mono` input to 4:2:0.)
- Bit depths: 8, 10, and 12 bpc.
- CPU SIMD: AVX2, AVX-512, NEON, and SVE2 when the host advertises it.
- GPU twins: `ssimulacra2_cuda`, `ssimulacra2_sycl`, and
  `ssimulacra2_hip`. (The Vulkan backend was removed in ADR-0726.)

The CPU scalar/SIMD path is bit-exact across the fork's host matrix. The Metal
twin offloads the pyramid blur and per-pixel multiply stages while keeping the
colour conversion, XYB, downsample and final combine on the host. The SYCL
twin (ADR-1363), CUDA twin (ADR-1391), and HIP twin (ADR-1390) run the whole
frame on the device (below).

### SYCL: device-resident, one readback per frame

`ssimulacra2_sycl` runs the whole frame on the device (ADR-1363): it uploads the
raw Y/U/V planes once, converts to linear RGB and XYB, blurs, forms the SSIM and
edge-difference sums and downsamples on the device, and reads back one 864-byte
block of per-scale sums. Name it, or pass `--backend sycl --feature ssimulacra2`,
which maps to the twin for inputs it can run and to the CPU extractor otherwise
(ADR-1359; `ssimulacra2_sycl` needs chroma planes and at least 8x8):

```shell
vmaf -r ref.yuv -d dis.yuv -w 3840 -h 2160 -p 420 -b 8 \
    --backend sycl --no_prediction --feature ssimulacra2_sycl -o out.json --json
```

The score is the CPU extractor's, bit for bit
([ADR-1446](../adr/1446-sycl-ssimulacra2-cpu-bits.md)). Colour conversion,
XYB, blurs and downsample match the CPU. The CPU then evaluates six terms per
pixel and channel in double precision and adds each into one double, pixel
after pixel. A SYCL device has no double precision, so the twin computes each
term's double in 64-bit integers (a sign, a 53-bit significand and an
exponent), the CPU's operations one for one, and forms the sums the way the
CUDA twin does (below): whole-number steps per chunk of 512 pixels in
parallel, one pass over the chunks, and term by term where the running sum
passes a power of two. Measured on an Arc A380 (xe driver) at
`--precision max`: 266 of 266 frames identical to `--backend cpu` (Netflix
576x324 at 8, 10, 12 and 16 bits and as 10-bit 4:2:2, both 1080p checkerboard
pairs, 200 frames of BBB 3840x2160), and with every `yuv_matrix`. Before
ADR-1446 the terms were pairs of floats added in a fixed tree and no frame
was identical; the score was up to 7.6e-11 from the CPU's, and
`ssimulacra2_sycl` scores stored before differ from new ones by that much.

The exact sums cost time. On the A380 a 3840x2160 frame takes 195 ms (84 ms
before ADR-1446) and a 576x324 frame 13.5 ms (5.3 ms before); the CPU
extractor takes about 125 ms and 1.4 ms on sixteen threads, so on this card
the CPU is the faster path for this metric at both sizes (before ADR-1446 the
twin was faster at 3840x2160). Most of the increase is the integer arithmetic
of the terms; the pass over the chunks runs on one lane per sum and is what
small frames pay (`T-SYCL-SSIMULACRA2-EXACT-THROUGHPUT-2026-10-02`). Before
ADR-1363 the twin copied about 4 GB per 4K frame between device and host.
Check and time with (`--vmaf` takes an absolute path):

```shell
ONEAPI_DEVICE_SELECTOR=level_zero:0 python3 scripts/dev/speed_gpu_parity.py \
    --backend sycl --feature ssimulacra2 --vmaf "$PWD/build/tools/vmaf" \
    --netflix-dir python/test/resource/yuv --bbb-dir testdata/bbb
```

The default bound of that script is 0: every frame must be bit-identical.

`ssimulacra2_sycl` rejects 4:0:0 (luma-only) input at init.

### CUDA: device-resident, one readback per frame

`ssimulacra2_cuda` runs the same chain on NVIDIA GPUs (ADR-1391). It reads the
Y/U/V planes that the CUDA picture pipeline uploads once for every CUDA
extractor, so the twin makes no copy of its own; its only transfer is one
864-byte block of per-scale sums per frame. Name it, or pass
`--backend cuda --feature ssimulacra2`, which
maps to the twin for inputs it can run and to the CPU extractor otherwise
(4:0:0 input and frames below 8x8):

```shell
vmaf -r ref.yuv -d dis.yuv -w 3840 -h 2160 -p 420 -b 8 \
    --backend cuda --no_prediction --feature ssimulacra2_cuda -o out.json --json
```

The score is the CPU extractor's, bit for bit
([ADR-1433](../adr/1433-cuda-ssimulacra2-cpu-sum-order.md)). Colour conversion,
XYB, blurs and downsample match the CPU, and CUDA devices have double
precision, so the per-pixel SSIM and edge terms are the CPU's own double
expressions. The CPU adds each term pixel after pixel into one double, and
every such add rounds; the twin returns the result of that loop without
running it on one thread. While the running sum stays between two powers of
two, adding a term moves it by a whole number of steps, so the device adds
those whole numbers per chunk of 1024 pixels in parallel and one pass over the
chunks puts them together; the few chunks in which the sum passes a power of
two are added term by term. Measured on an RTX 4090 at `--precision max`: 113
of 113 frames identical to `--backend cpu` (Netflix 576x324 at 8, 10, 12 and
16 bits, both 1080p checkerboard pairs, BBB 3840x2160). Before ADR-1433 the
terms were added in a fixed tree and the score was up to 7.3e-11 from the
CPU's. A 3840x2160 frame takes 15.6 ms (7.8 ms with the tree; the CPU
extractor takes 126 ms on sixteen threads). Before ADR-1391 the twin copied
every scale between device and host. Check and time it with (`--vmaf` takes an
absolute path):

```shell
python3 scripts/dev/speed_gpu_parity.py --backend cuda --feature ssimulacra2 \
    --vmaf "$PWD/build/tools/vmaf" \
    --netflix-dir python/test/resource/yuv --bbb-dir testdata/bbb
```

The default bound of that script is 0: every frame must be bit-identical.

The twin keeps the equivalent of 14.5 full-size three-plane float buffers in
device memory (the linear-RGB pyramid, XYB, and the two passes of the five
blurs): about 1.4 GB at 3840x2160. The sums add 3.8 MB at that size.

### HIP: device-resident, tiled row pass

`ssimulacra2_hip` runs the whole frame on the device (ADR-1390): it uploads
the raw Y/U/V planes once into pinned staging in `submit()`, converts to
linear RGB and XYB, executes IIR blurs with a tiled shared-memory row pass
(`SS2H_ROW_TILE` rows per wavefront, single-wave blocks, two-slot ring, register
prefetch), evaluates the per-pixel SSIM and edge terms in double precision,
adds them with the result of the CPU's loops, and downsamples on the device. A
single 864-byte readback of per-scale sums occurs in `collect()`. Name it, or
pass `--backend hip --feature ssimulacra2` (ADR-1359):

```shell
vmaf -r ref.yuv -d dis.yuv -w 3840 -h 2160 -p 420 -b 8 \
    --backend hip --no_prediction --feature ssimulacra2_hip -o out.json --json
```

The score is the CPU extractor's, bit for bit
([ADR-1445](../adr/1445-hip-ssimulacra2-cpu-sum-order.md)). The twin evaluates
the CPU's own double expressions for the SSIM and edge terms and adds them the
way the CUDA twin does (above): whole-number steps per chunk of 1024 pixels in
parallel, one pass over the chunks, and term by term where the running sum
passes a power of two. Measured on an AMD gfx1036 (ROCm 7.2.4) at
`--precision max`: 178 of 178 frames identical to `--backend cpu` (Netflix
576x324 at 8, 10, 12 and 16 bits and as 10-bit 4:2:2, both 1080p checkerboard
pairs, Sparks 480x270, full-range noise at four bit depths, a bright 16-bit
1080p pair, 48 frames of BBB 3840x2160), and with every `yuv_matrix`. Before
ADR-1445 the terms were pairs of floats added in a fixed tree and no frame was
identical; the score was up to 7.6e-11 from the CPU's, and `ssimulacra2_hip`
scores stored before differ from new ones by that much.

The exact sums cost time. On the gfx1036 a 1920x1080 frame takes 167 ms (58 ms
before ADR-1445) and a 3840x2160 frame 662 ms (234 ms before); the CPU
extractor takes 124 ms for the latter on sixteen threads, so on an integrated
GPU the CPU is the faster path for this metric. Most of the increase is the
double-precision terms, evaluated twice per scale, and the integer steps
(`T-HIP-SSIMULACRA2-EXACT-THROUGHPUT-2026-10-02`). Check and time with
(`--vmaf` takes an absolute path):

```shell
python3 scripts/dev/speed_gpu_parity.py --backend hip --feature ssimulacra2 \
    --vmaf "$PWD/build-hip/tools/vmaf" \
    --netflix-dir python/test/resource/yuv --bbb-dir testdata/bbb
```

The default bound of that script is 0: every frame must be bit-identical.

`ssimulacra2_hip` rejects 4:0:0 input at init.

### Cross-compiler bit-exactness (FMA unification)

`picture_to_linear_rgb` performs the YCbCr→linear-RGB conversion using a
fused-multiply-add (FMA) chain on every code path: scalar uses `fmaf()` and the
AVX2 / AVX-512 / NEON main loops use `_mm256_fmadd_ps` / `_mm512_fmadd_ps` /
`vfmaq_f32`. Earlier revisions used explicit `mul + add` pairs, but icx with
`-mfma` auto-fused them while gcc kept them separately-rounded, producing
sub-ULP divergence between compilers. Unifying on FMA preserves the
left-to-right associativity of `G = Yn + cb_g*Un + cr_g*Vn` (two chained FMAs)
while delivering a single, identically-rounded result on every supported
compiler. See ADR-0891 for the analysis and alternatives.

ADR-0891 originally reached the four SIMD kernels and the SIMD test's own
scalar reference, but not the five shipped copies that are not SIMD: the
scalar fallback in `core/src/feature/ssimulacra2.c` and the host-side
conversions in the CUDA, HIP, Metal and SYCL twins. Those kept a plain
`mul + add`, so until ADR-1205 a host **without** AVX2 scored `ssimulacra2`
differently from one with it, and every GPU backend disagreed with the CPU.

The size of that disagreement is worth knowing when reading any
`ssimulacra2` number: the pipeline is ill-conditioned downstream. The
edge-diff term computes `|img - blur(img)|`, a catastrophic cancellation, and
pooling takes a 4-norm, which is dominated by the few largest survivors. A
~1 ULP difference in linear RGB therefore grew to a **2.62e-03** difference in
the final score — not the ~1e-7 a single rounding step would suggest. After
ADR-1205 every shipped path uses the same FMA chain and CPU-vs-CUDA agreement
is ~2.8e-09.

If you add another `ssimulacra2` code path, the conversion must be
`fmaf()`-based and in the ADR-0891 order. Note that
`core/test/test_ssimulacra2_simd.c` compares the SIMD kernels against a
*private* scalar reference rather than the shipped function, so it will not
catch a shipped copy that drifts.

## Limitations

- Chroma is nearest-neighbour upsampled to luma resolution before colour
  conversion.
- The recursive Gaussian coefficient derivation is pinned to the shipped
  sigma used by the libjxl reference path; arbitrary sigma values are not a
  user option.
- Scores are useful as a perceptual ranking signal, not as an MOS-calibrated
  VMAF replacement.

## See Also

- [Feature extractor matrix](features.md#ssimulacra-2-perceptual-similarity-in-xyb-space)
- [ADR-0130](../adr/0130-ssimulacra2-scalar-implementation.md)
- [ADR-0164](../adr/0164-ssimulacra2-snapshot-gate.md)
- [ADR-0206](../adr/0206-ssimulacra2-cuda-sycl.md)
