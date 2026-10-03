<!-- markdownlint-disable MD013 MD060 -->
# Research-1477: SpEED against Netflix master, and what a GPU twin has to do to return an fp64 `log2`

- **Status**: Active
- **Workstream**: [ADR-1477](../adr/1477-speed-upstream-double-math.md), [ADR-1358](../adr/1358-sycl-speed-device-resident-linalg.md), [ADR-1380](../adr/1380-cuda-speed-device-resident-pipeline.md), [ADR-1384](../adr/1384-hip-speed-device-resident.md), [ADR-1430](../adr/1430-cuda-speed-chroma-log2f-bound.md)
- **Last updated**: 2026-10-02

## Question

The fork's `speed_chroma` and `speed_temporal` differ from Netflix's on most
frames. Which expressions cause it, do Netflix's forms depend on the compiler
or the architecture, and how can the CUDA, HIP and SYCL twins return the CPU's
scores once the CPU computes its logarithms in fp64 with the host's C
library?

## Sources

- Netflix/vmaf `libvmaf/src/feature/speed.c` at `9e48141b` (master) and a
  GCC 16.2.1 release build of `cea2b4d8`; the two commits differ in the
  aarch64 ADM kernel only.
- Fork `core/src/feature/speed.c`, `speed_internal.c`, and the three device
  chains: `cuda/speed/speed_score.cu`, `hip/speed/speed_hip_device.h`,
  `sycl/speed_sycl_pipeline.cpp`.
- Host: Ryzen 9 9950X3D, glibc 2.44, GCC 16.2.1, clang 23.1.1, icx 2026.0; RTX
  4090 (CUDA 13.4), gfx1036 (ROCm 7.2.4), Arc A380 (xe, Level Zero); aarch64
  through `qemu-aarch64` with the sysroot's glibc.

## Findings

### 1. Three expressions, all from the port

| Netflix (`speed.c` line) | Port #213 (`32f275788`) |
|---|---|
| 418, 423: `float s1 = 1.0 / sqrt(1 + t * t);` | `1.0f / sqrtf(1.0f + t * t)` |
| 802, 803: `log2(L * S + sigma_nn) + log2(2 * M_PI * M_E)`, added to a `float` | `log2f(...) + log2f(2.0f * (float)M_PI * (float)M_E)` |
| 897 to 928: `entropy * log2(1 + variance)`, with `/ 2.0` and `0.75 *` in modes 3 to 6 | `log2f(1.0f + ...)`, `/ 2.0f`, `0.75f *` |

Each Netflix statement computes in fp64 and rounds to `float` once. The
other fp32 spellings the fork has (`sqrtf` of an fp32 norm, `/ 2.0f` in
`trailing_eigenvalue()`, #1209) give the same values and stay.

### 2. Against Netflix, before and after

A harness reads every per-frame value through the C API at `%.17g` from a
static build of each tree (`--cpumask` 63 scalar, 0 default dispatch, 48
AVX2). Identical values of all compared, and the largest difference:

| Output | Before, scalar | After, scalar | After, default dispatch | After, AVX2 |
|---|---|---|---|---|
| `speed_chroma_u` | 32 of 261, 2.3e-5 | 261 of 261 | 261 of 261 | 213 of 213 |
| `speed_chroma_v` | 49 of 261, 2.3e-5 | 261 of 261 | 261 of 261 | 213 of 213 |
| `speed_chroma_uv` | 47 of 261, 1.1e-5 | 261 of 261 | 261 of 261 | 213 of 213 |
| `speed_temporal` | 130 of 320, 6.6e-4 | 320 of 320 | 320 of 320 | 272 of 272 |
| `speed_chroma`, 20 option sets | 1438 of 3564, 3.8e-5 | 3564 of 3564 | 3564 of 3564 | 3564 of 3564 |
| `speed_temporal`, 13 option sets | 528 of 1125 | 1019 of 1125 | 1021 of 1122 | 1021 of 1077 |
| `vmaf_v1.0.16_3d0h` score | 18 of 204, 1.58e-5 | 204 of 204 | 204 of 204 | not run |
| `vmaf_v1.0.16_3d0h_2160` score | 111 of 204, 1.55e-5 | 204 of 204 | 204 of 204 | not run |
| `vmaf_v1.0.16_1d5h_2160` score | 18 of 204, 1.75e-5 | 204 of 204 | 204 of 204 | not run |
| `vmaf_v1.0.16_5d0h` score | 65 of 204, 2.47e-5 | 204 of 204 | 204 of 204 | not run |

The AVX2 column has fewer frames because the Netflix run with that mask has
no 3840x2160 clip.

What still differs in the `speed_temporal` option row, and why:

| Probe | Identical | Class |
|---|---|---|
| `speed_max_val=3.0` | 52 of 105 | deliberate: the fork clamps ([ADR-1301](../adr/1301-speed-nonfinite-score-fails-frame.md)), Netflix ignores the option on `speed_temporal` |
| `speed_prescale=2.0:speed_prescale_method=lanczos4` | 4 of 57 (6 of 54 default dispatch, 6 of 9 AVX2) | deliberate: Netflix reads past its frame buffers at a prescale above 1, the fork sizes them for the scaled frame (#1643, [ADR-1480](../adr/1480-speed-frame-buffers-prescale-above-one.md)) |

Probes that give no value on one side are not counted: frames with a plane
below 80x80 (Netflix crashes, exit by signal 11; the fork refuses the frame
with `-EINVAL`) and 4:0:0 input to `speed_chroma` (Netflix emits nothing,
the fork refuses); both are
[ADR-1481](../adr/1481-extractor-failure-fails-the-run.md).

### 3. Netflix's form does not depend on the compiler or the architecture

The same probes on four builds of the branch, compared with its x86-64 GCC
build: x86-64 clang, aarch64 GCC and aarch64 clang (both under
`qemu-aarch64`). All four return the same bits on every probe: 261 + 261 +
261 + 320 default values and 3564 + 1173 option values, scalar and default
dispatch. An icx build (Intel's `libimf` instead of glibc) returns the GCC
build's bits on the 3409 values of the twin comparison below.

The C libraries do not agree on the last bit of an fp64 `log2`, but the sum
is rounded to `float`, which absorbs a difference in the 53rd bit unless the
sum lies within that distance of a `float` rounding boundary: about one
evaluation in 2^28 for each unit of difference.

### 4. The Givens statement in fp32, on every input

`create_givens()` forms `t` as the quotient of the smaller magnitude by the
larger, so `|t| <= 1` and `u = 1 + t * t`, an fp32 value, is one of the
2^23 + 1 floats of [1, 2]. Over all of them:

| Form | Differs from `(float)(1.0 / sqrt((double)u))` on |
|---|---|
| `1.0f / sqrtf(u)` (the port) | 2,907,055 inputs |
| root, reciprocal, two fused residuals, one correction (three variants tried, with and without divisions in the correction) | 0 inputs |

The variant without a division in the correction is `speed_givens_unit()`
(`core/src/feature/speed_givens.h`). GCC and clang builds of it agree.
`test_speed_upstream_form` repeats the enumeration.

The test's comparison with a Netflix build's values depends on the C
library: Netflix's statements call `log2()`, and those values are glibc's.
glibc 2.39 (Ubuntu 24.04), 2.41 (Debian), 2.43 (Ubuntu 26.04) and 2.44
return the same bits for each of the 180,565 `log2()` arguments the test
evaluates, measured with a shim that records them and a probe run in each
image. The MSVC runtime's, Apple's and musl's `log2()` were not measured,
so the test runs that comparison on glibc only and reports it as skipped
elsewhere. What holds on every C library is the comparison of `speed.c`'s
statements with Netflix's, both evaluated with the host's `log2()`.

### 5. Why the logarithms are on the host

A twin needs, per block and channel, 25 times
`(float)((double)entropy + (log2((double)x) + K))`, and per block one or two
products `(float)(entropy * log2(argument))` whose argument is an fp64 sum in
weighting modes 3 to 6 (mode 5 is what `vmaf_v1.0.16` uses).

| Route | What it gives |
|---|---|
| Device `log2` in fp32 pairs, rounded to `float` (before) | the port's fp32 form: matches no CPU that computes in fp64 |
| Correctly rounded fp64 `log2` on the device | equals a CPU whose `log2` is correctly rounded. glibc's and libimf's are not, so the cell keeps a bound. Needs about 128 bits of working precision for fp64 arguments, in fp64 on two devices and in integers on the third |
| The host's `log2` on the host | the CPU extractor's own calls: equal by construction, on any library |

The work moved to the host is small, because SpEED scores a plane decimated
by 16 in blocks of 5x5:

| Frame | Feature | Channels | Blocks | `log2` calls per frame, at most | Host tail (one thread) |
|---|---|---:|---:|---:|---:|
| 1920x1080 | `speed_chroma` | 4 | 72 | 7,488 | 38 us |
| 1920x1080 | `speed_temporal` | 2 | 312 | 16,224 | 81 us |
| 3840x2160 | `speed_chroma` | 4 | 312 | 32,448 | 162 us (180 us in mode 5) |
| 3840x2160 | `speed_temporal` | 2 | 1296 | 67,392 | 379 us |

(`speed_internal_gpu_tail_scores()` in a loop of 2000 calls, best of 7, GCC
`-O2`, glibc 2.44, Ryzen 9 9950X3D, load average about 50.) The block the
twin reads back is 108 bytes per channel plus four bytes per block and
channel: 10.6 kB for a 3840x2160 `speed_temporal` frame.

### 6. The twins against the CPU

`--precision max`, twin requested by name, against `--backend cpu` of the
same binary. 21 clips at the default options (20 for `speed_chroma`: one is
too small for it): the Netflix 576x324 pair at 8, 10, 12 and 16 bits, as
10-bit 4:2:2 and as 8- and 12-bit 4:4:4, both 1920x1080 checkerboard pairs,
Sparks 480x270, noise at four bit depths, a bright 16-bit 1920x1080 pair, a
1920x1080 and a 10-bit 1280x720 gradient, akiyo 352x288, a 256x144 clip, and
48 frames each of BBB 1920x1080 and 3840x2160. 18 option sets on four of the
clips: the `vmaf_v1.0.16` options, weighting modes 1 to 6, `kernelscale` 0.5
and 1.5, `speed_sigma_nn`, `speed_nn_floor`, prescale 0.5 (nearest,
bilinear, bicubic) and 2.0 (`lanczos4`), `speed_use_ref_diff`.

| Device, CPU build | `speed_chroma` default | `speed_chroma` options | `speed_temporal` default | `speed_temporal` options |
|---|---|---|---|---|
| RTX 4090, GCC and glibc | 759 of 759 | 2052 of 2052 | 256 of 256 | 342 of 342 |
| gfx1036, GCC and glibc | 759 of 759 | 2052 of 2052 | 256 of 256 | 342 of 342 |
| Arc A380, icx and libimf | 759 of 759 | 2052 of 2052 | 256 of 256 | 342 of 342 |

Before, a GCC build's CPU differed from each twin on a few values (13 of 789
on the RTX 4090, 1.4e-6 at most; ADR-1430).

`test_hip_speed_device_math` replays the HIP device header and the host tail
on the host against the CPU extractor, in 15 cases on the Netflix-derived
576x324 pair and 3 on synthetic frames (prescale methods, weighting modes,
`speed_use_ref_diff`, 10 bits, a flat chroma plane), and asserts `==`; it
needs no device and no libm seam any more.

### 7. Time per frame

`vmaf --backend <b> --feature <twin>`, wall time, `(t(long) - t(short)) /
(long - short)` with 24 and 384 frames at 1920x1080 and 10 and 200 frames at
3840x2160, the base build (`origin/master` `10d6a0505`) and the branch
alternating sample by sample, 15 samples each (11 or 9 for the two slowest
rows), load average 32 to 53 from other work on the host. Median before,
median after, and the median of the paired differences, in ms:

| Twin | 1920x1080 before / after (paired) | 3840x2160 before / after (paired) |
|---|---|---|
| `speed_chroma_cuda` | 0.698 / 0.704 (-0.003) | 2.482 / 2.545 (-0.036) |
| `speed_temporal_cuda` | 0.611 / 0.634 (+0.028) | 3.292 / 3.125 (-0.116) |
| `speed_chroma_hip` | 1.795 / 1.791 (-0.022) | 6.357 / 6.664 (-0.026) |
| `speed_temporal_hip` | 4.202 / 4.184 (-0.027) | 17.007 / 16.670 (-0.356) |
| `speed_chroma_sycl` | 3.465 / 3.395 (-0.123) | 6.482 / 6.066 (-0.430) |
| `speed_temporal_sycl` | 3.297 / 3.144 (-0.044) | 9.662 / 9.513 (+0.035) |

No row is outside the spread of its samples (a `speed_temporal_cuda` sample
ranges from 0.53 to 1.09 ms at 1920x1080). The host tail's time is about
what the device no longer spends on the fp32-pair `log2` and the score
kernel.

A first version of `speed_givens_unit()` divided three times instead of
once. The SYCL twins, whose eigenvalue sweep runs on one work-item, were
0.2 to 0.3 ms per 1920x1080 frame slower with it (2.58 to 2.80 ms for
`speed_chroma_sycl`); the version without those divisions is the one
measured above.

## Reproduce

```sh
# CPU, tail and Givens, no device
meson setup build core -Denable_float=true && ninja -C build
python3 scripts/ci/run_meson_test.py -- -C build test_speed_upstream_form \
    test_speed_upstream_form_foreign_libm test_speed_simd

# a twin against the CPU of its build (exit 0 = every value identical)
python3 scripts/dev/speed_gpu_parity.py --backend cuda --vmaf build-cuda/tools/vmaf --no-timing
python3 scripts/dev/speed_gpu_parity.py --backend hip --vmaf build-hip/tools/vmaf --no-timing
ONEAPI_DEVICE_SELECTOR=level_zero:0 python3 scripts/dev/speed_gpu_parity.py --backend sycl \
    --vmaf build-sycl/tools/vmaf --no-timing

# the gate cells, tolerance 0
python3 scripts/ci/cross_backend_parity_gate.py --vmaf-binary build-cuda/tools/vmaf \
    --reference python/test/resource/yuv/src01_hrc00_576x324.yuv \
    --distorted python/test/resource/yuv/src01_hrc01_576x324.yuv \
    --width 576 --height 324 --backends cpu cuda --features speed_chroma speed_temporal
```
