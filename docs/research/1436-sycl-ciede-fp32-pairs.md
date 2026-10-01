<!-- markdownlint-disable MD013 MD060 -->
# Research-1436: ciede2000 on SYCL without fp64 — each fp32 piece of the old twin on its own, elementary functions on fp32 pairs, and where the last pixels come from

- **Status**: Active
- **Workstream**: [ADR-1436](../adr/1436-sycl-ciede-cpu-arithmetic.md), [ADR-1426](../adr/1426-cuda-ciede-cpu-arithmetic.md), [ADR-0220](../adr/0220-sycl-fp64-fallback.md), [ADR-1395](../adr/1395-sycl-kernels-no-scratch.md)
- **Last updated**: 2026-10-01

## Question

[Research-1426](1426-cuda-ciede-cpu-arithmetic.md) showed that `ciede.c`
computes in double and stores in float, and that a twin written in those
types is within 1.4e-11 of the CPU. `ciede_sycl` was fp32 throughout and up
to 1.14e-5 away. A SYCL kernel has no fp64 type. Which of the twin's fp32
pieces carried the error, can the fp64 arithmetic and the fp64 math-library
calls be evaluated on fp32 pairs closely enough that the float results are
the CPU's, and what is left afterwards?

## Sources

- CPU: `core/src/feature/ciede.c` (`get_lab_color()`, `rgb_to_xyz_map()`,
  `xyz_to_lab_map()`, `ciede2000()`, `get_h_prime()`, `get_upcase_t()`,
  `get_r_sub_t()`, `extract()`).
- The reference's statements with written-out promotions:
  `core/src/feature/cuda/integer_ciede/ciede_device.h` (ADR-1426), compiled
  for the host.
- SYCL: `core/src/feature/sycl/integer_ciede_sycl.cpp` at master `e955b2fe6`
  (before) and on `fix/sycl-ciede-cpu-arithmetic` (after),
  `core/src/feature/sycl/sycl_ciede_math.h` and
  `core/src/feature/sycl/sycl_ff_math.h` (new),
  `core/src/feature/sycl/sycl_exact_fp.h`.
- Host: Arc A380 (xe driver, Level Zero, compute runtime 26.35.39758), icpx
  2026.0.0, gcc 16.2.1, glibc, Linux 7.2.8, Ryzen 9 9950X3D. `meson setup
  build-sycl core -Denable_sycl=true -Denable_cuda=false
  -Dsycl_icpx_aot_targets= --buildtype=release -Db_lto=false`; the CPU
  reference is a GCC build unless a line says otherwise.
- Fixtures, `--precision max`: the Netflix pair `src01_hrc00/01_576x324` at 8
  bits (48 frames) and its 10-, 12- and 16-bit and 4:2:2 10-bit versions (3
  frames each), the checkerboard pairs `checkerboard_1920_1080_10_3_0_0`
  against `_1_0` and `_10_0` (3 frames each), and BBB 3840x2160.

## Findings

### 1. The old twin's pieces, one at a time

Each row puts one property of the old twin back into the corrected one and
leaves the rest corrected. Largest absolute difference of `ciede2000` against
`--backend cpu`; BBB over its first 20 frames. No frame of the Netflix pair or
of BBB is identical in any row but the first.

| Property put back | Netflix 576x324 | Checkerboard 1 px | Checkerboard 10 px | BBB 3840x2160 |
|---|---:|---:|---:|---:|
| none (the corrected twin) | 6.9e-13 | 0 | 0 | 9.7e-12 |
| the linear Lab branch as `7.787 t + 16 / 116` | 1.12e-5 | 0 | 0 | 1.05e-6 |
| the reference's constants as fp32 values | 5.3e-7 | 8.6e-7 | 8.6e-7 | 6.7e-7 |
| per-pixel values added in float in groups of 256, the groups in double | 2.0e-7 | 1.2e-6 | 1.7e-6 | 8.0e-8 |
| `x^2.4` from the device's fp32 `pow` | 2.3e-7 | 0 | 0 | 5.9e-7 |
| the YUV to RGB and RGB to XYZ arithmetic in fp32 | 2.1e-7 | 8.6e-7 | 8.6e-7 | 2.1e-7 |
| the cube root from the device's fp32 `cbrt` | 5.0e-8 | 0 | 0 | 3.8e-7 |
| `L`, `a`, `b` formed in fp32 | 3.4e-8 | 0 | 0 | 2.2e-8 |
| the hue angle from the device's fp32 `atan2` | 1.5e-8 | 0 | 0 | 3.1e-8 |
| the chroma magnitudes from an fp32 `sqrt` of fp32 squares | 5.3e-9 | 0 | 0 | 5.9e-8 |
| the final expression in fp32 | 2.7e-9 | 0 | 0 | 3.2e-9 |
| the four cosines of `T` from the device's fp32 `cos` | 5.4e-10 | 0 | 0 | 1.1e-9 |
| the two sines from the device's fp32 `sin` | 3.8e-10 | 0 | 0 | 3.3e-9 |
| `exp` from the device's fp32 `exp` | 7.0e-11 | 0 | 0 | 7.7e-9 |
| the old twin | 1.14e-5 | 0 | 0 | 1.32e-6 |

One line was almost all of the old twin's error. `xyz_to_lab_map()` computes
`(KAPPA * c + 16) / 116` with `KAPPA = 24389 / 27` for a dark component; the
twin had the textbook form `7.787 t + 16 / 116`, whose constant is the same
number rounded to four digits (5e-6 of its value). Every other piece is a
type or a math-library call, and none of them alone exceeds 9e-7. They do
not add up to nothing either: with only the formula corrected the twin would
still be an fp32 twin at about 1e-6.

The checkerboards are black and white: every pixel is on the neutral axis,
where the hue terms vanish, so only the pieces that touch the lightness
show there.

### 2. Elementary functions on fp32 pairs

A pair `hi + lo` (`sycl_exact_fp.h`) carries about 48 bits. `ciede.c` rounds
every fp64 result to float within a few operations, so what the kernel needs
from an fp64 expression is its value to somewhat better than a float: a
result within 2^-44 rounds like the fp64 one unless the value lies within
about 2^-20 of a float step of a rounding boundary. Largest errors against
the host's extended-precision math library, 20 million arguments each over
what ciede2000 passes:

| Function | Method | Largest error |
|---|---|---:|
| `sqrt` | the device's fp32 root, one Newton correction from the exact residual | 2^-46.4 |
| `cbrt` | one Halley step from the device's `cbrt` | 2^-45.4 |
| `x^2.4` | `(x * x^(1/5))^2`, the fifth root by one Halley step from the device's `pow(x, 0.2f)` | 2^-44.0 |
| `x^7` (arguments above 0.01) | four multiplications | 2^-45.2 |
| `exp` | `2^k e^r`, three-part `ln 2`, 14 Taylor terms (the last seven in fp32) | 2^-47.2 |
| `sin`, `cos` | `k pi / 16 + r`, three-part `pi / 16`, 32-entry table, 4 and 5 series terms | 2^-46.5 |
| `atan2` | quotient in `[0, 1]`, 17-entry table of `atan(j / 16)`, 4 series terms | 2^-45.6 |

A Halley step for `y^n = x` cubes the relative error of its start, so the
device's `cbrt` and `pow`, good to a few float steps, give a root good to the
pair's precision. Rounded to float, the results equal `(float)` of the fp64
library function on every one of the 20 million arguments for `sqrt`, `sin`,
`cos` and `60 * exp()`, and on all but 1 or 2 for `cbrt`, `x^2.4` and
`atan2`.

`pow(c, 1.0 / 3.0)` in the reference has the double nearest to one third as
its exponent, not one third; the difference is below 2^-53 of the result for
every argument and does not reach a pair.

The series for `sin`, `cos` and `atan2` read their tables with an index known
only at run time. A constant array indexed that way inside a kernel lives in
private memory, which is scratch memory on Intel GPUs (ADR-1395); the tables
are therefore copied to device memory once and reached through a pointer.

### 3. The pixel against the fp64 statements

`sycl_ciede_math.h` on the host against `ciede_device.h` (the reference's
statements in fp64) on the same samples, 2 million colour pairs per bit depth
(8, 10, 12, 16):

| Colour pairs | Pixels whose float differs | Mean difference |
|---|---:|---:|
| independent reference and distorted colours | 0 of 8 million | 0 |
| distorted within a few levels of the reference | 5 of 8 million | 3e-13 |
| identical | 0 of 2 million | 0 |

One of the five differs by 2e-6 of its value instead of one float step: a
float one step off early in the chain (a chroma magnitude near zero) is
amplified by the hue terms that follow. That sensitivity is the reference's
own.

### 4. Where the last pixels come from

Three frames of BBB 3840x2160, 8 294 400 pixels each, the device's values
against the fp64 statements compiled twice, once with the build's `powf` and
once with a correctly rounded one (`(float)pow((double)x, (double)y)`).
Pixels that differ from the device's:

| Reference | Frame 10 | Frame 100 | Frame 190 |
|---|---:|---:|---:|
| fp64 statements, correctly rounded `powf` | 0 | 0 | 0 |
| fp64 statements, glibc `powf` (the GCC build) | 18 | 60 | 64 |
| fp64 statements, Intel `powf` (the icx build) | 7 | 30 | 8 |

On these frames every pixel the twin gets differently is a pixel where the
host's `powf` is not correctly rounded, the cause ADR-1426 found for the CUDA
twin; the pairs lose none of 24.9 million. The 18 pixels of frame 10 move
its score by 1.4e-13. The device's values and the host evaluation of the same
header agree on every pixel.

`get_r_sub_t()` calls `powf(c, 7)` and `powf(degrees, 2)`. Reproducing a
particular `powf` is not attempted: glibc's and Intel's differ from each
other, so a twin that matched one would not match the other.

### 5. Kernel shape and time

The per-pixel function runs about two dozen pair functions. Left to the
compiler some of them stayed calls; a call inside a kernel takes its frame
from scratch memory, the audit reported 3.4 KiB of it, and the first build
scored 27 dB off on the A380. With the pixel function flattened into the
kernel (`__attribute__((flatten, always_inline))`) and the header's functions
always inlined, the audit reports none (118 kernels).

Kernel shapes on the A380 at 3840x2160 (five runs each, with the division
of the next paragraph already in place):

| Sub-group size | Default register file | 256-entry register file |
|---|---:|---:|
| 8 | 75.7 ms | 147.1 ms |
| 16 | 50.4 ms | 80.7 ms |
| 32 | 37.7 ms, 16 KiB of spills | 78.1 ms |

SIMD-32 with the default file is the fastest and is not allowed: its spills
are scratch memory. The kernel pins SIMD-16 with the default file.

The first version called `sycl_exact_fp.h`'s `ff_div()` and `sqrt_rn()`,
whose partial results are correctly rounded (a reciprocal, two FMA
corrections and a comparison of both neighbours each). A pair quotient or
root does not need that: the second partial result is taken from the exact
residual of the first, whatever the first's rounding. With the device's own
division and square root in their place the per-pixel values are the same on
every test and the frame takes 50 ms instead of 82.

Where the time goes, from kernels with a stage removed (medians of 5 and 6
runs, ms per 3840x2160 frame):

| Stage | First version | With the device's division |
|---|---:|---:|
| no per-pixel work: uploads, read-back of one float per pixel, the host's sum | 11.2 | 10.8 |
| the two Lab conversions | 42.4 | 19.0 |
| of which the six `x^2.4` | 27.0 | 8.2 |
| of which the six cube roots | 10.2 | 5.7 |
| of which the linear arithmetic | 5.2 | 5.1 |
| the difference formula | 37.1 | 20.6 |
| of which the four cosines of `T` | 9.0 | 6.6 |
| of which `R_T` (`exp`, `sin`, `x^7`, a root) | 6.4 | 3.1 |
| of which the two hue angles (`atan2`) | 10.7 | 2.5 |
| of which the rest (five roots, the half-angle sine, four float divisions, the final expression) | 11.1 | 8.3 |
| whole kernel | 83.2 | 50.6 |

The device's `pow(x, 0.2f)` that starts each fifth root accounts for 3.0 ms
of the 8.2, the device's `cbrt` for 1.4 of the 5.7.

Through the `vmaf` tool on the A380, medians of 11 alternating pairs
(`(t(52) - t(2)) / 50`, host load average 9 to 14):

| | Before | After |
|---|---:|---:|
| BBB 3840x2160, ms per frame | 16.18 | 50.27 |
| Netflix 576x324, ms per frame | 0.45 | 1.23 |
| control: `float_psnr` 3840x2160 | 3.95 | 3.91 |

The paired difference at 3840x2160 is 33.9 ms. The first version measured
81.5 ms and 1.77 ms. The CPU extractor of the GCC build takes 2 525 ms per
3840x2160 frame on one thread. The twin reads back one float per pixel (33 MB
at 3840x2160) and the host adds 8.3 million values in order.

### 6. What is left

- The residual of section 4, a property of the host's `powf`.
- The score goes through the host's `log10`; a GCC build and an icx build
  differ by one unit in the last place on some frames
  (`T-ICX-LIBIMF-HOST-MATH-2026-10-01`), CPU extractor against CPU extractor.
- Throughput: `T-SYCL-CIEDE-EXACT-THROUGHPUT-2026-10-01`. Candidates not
  tried, by the table above: the two Lab conversions in a kernel of their
  own, so that each kernel is small enough for SIMD-32 without spills (the
  whole kernel at SIMD-32 is 38 ms); one `sin_cos` of the mean hue and
  angle-addition formulas for the four cosines of `T` (6.6 ms, three of four
  table reductions saved); an fp32 Newton start for the fifth root instead of
  the device's `pow` (3 ms).

## Reproduce

```bash
ONEAPI_DEVICE_SELECTOR=level_zero:0 python3 scripts/dev/speed_gpu_parity.py \
    --backend sycl --feature ciede --vmaf "$PWD/build-sycl/tools/vmaf" \
    --max-abs-diff 1e-9
flock ~/.cache/vmafx-locks/sycl-a380.lock timeout 300 \
    build-sycl/test/test_sycl_ciede_math
flock ~/.cache/vmafx-locks/sycl-a380.lock timeout 300 \
    build-sycl/test/test_sycl_ciede_parity
python3 scripts/dev/gen_sycl_ff_math.py --check
```
