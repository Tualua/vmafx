<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1448: `ciede_hip` runs the CPU's arithmetic in fp32 pairs, from the header the SYCL twin runs; what remains is glibc's `powf` and the last bits of a pair

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `hip`, `sycl`, `gpu-parity`, `numerics`, `ciede`, `testing`, `ci`, `rc3`, `fork-local`

## Context

[ADR-1426](1426-cuda-ciede-cpu-arithmetic.md) found that `ciede.c` computes
in double and stores in float, and that a twin which computes in fp32 with
another form of the formula and adds per block is 1.1e-5 from it for reasons
of its own, not because of transcendentals. It rewrote `ciede_cuda` in the
reference's types. [ADR-1436](1436-sycl-ciede-cpu-arithmetic.md) did the same
for `ciede_sycl` on a device without an fp64 type, with every fp64 value as
an fp32 pair. `ciede_hip` still had the fp32 formulation
(`T-GPU-CIEDE-CPU-ARITHMETIC-2026-10-01`). Measured on a gfx1036 (ROCm 7.2.4)
at `--precision max` against `--backend cpu` on `origin/master` 80c5a0332, no
frame of 178 equalled the CPU:

| Fixture | Frames | Identical | Max abs diff |
|---|---|---|---|
| Netflix 576x324, 8 bit | 48 | 0 | 1.1e-5 |
| Checkerboard 1 px and 10 px, 1920x1080 | 6 | 0 | 8.6e-7 |
| Netflix 576x324, 10, 12 and 16 bit | 9 | 0 | 9.4e-6 |
| Netflix 576x324, 10-bit 4:2:2 | 48 | 0 | 1.1e-5 |
| Sparks 480x270, 10 bit | 5 | 0 | 1.1e-6 |
| BBB 3840x2160 | 48 | 0 | 1.4e-6 |
| Full-range noise 576x324 at 8, 10, 12, 16 bit | 12 | 0 | 2.3e-7 |
| Bright 16 bit, 1920x1080 | 2 | 0 | 1.6e-6 |

An AMD device has fp64, so the first version of this change compiled the CUDA
twin's fp64 statements for it. The result was right (115 of 178 frames
identical, the rest within 1.4e-11) and too slow: 318.4 ms per 1920x1080
frame instead of 18.3, and 1 317 ms per 3840x2160 frame instead of 75.0, 17
times. The device's fp64 `pow`, `atan2`, `sin`, `cos` and `exp` are the cost
(twelve `pow` per pixel pair in the two L\*a\*b\* conversions alone, 198 of
the 318 ms). That version was not merged.

## Decision

We will make `ciede_hip` evaluate `ciede.c`'s statements on fp32 pairs, from
the header `ciede_sycl` uses, add the per-pixel values in the reference's
order, and list it as a math-library twin with the measurement below.

**One header for two twins.** The arithmetic of
`core/src/feature/sycl/sycl_ciede_math.h` and the pair functions of
`core/src/feature/sycl/sycl_ff_math.h` were SYCL code only by the functions
they called (`sycl::sqrt`, `sycl::cbrt`, `sycl::pow`, `sycl::rint`,
`sycl::fabs`, `sycl::ldexp`) and the pair operations they imported. They move
to the backend-neutral `core/src/feature/ciede_ff_math.h` and
`core/src/feature/ff_math.h`, with those functions as macros (`VMAF_FF_SQRT`
and so on) and the pair operations as the namespace `vmaf_ffm_base`, which
the including file provides. The two SYCL headers keep their names and now
only define the SYCL primitives and include the shared headers, so no other
SYCL source changes. After preprocessing the SYCL kernel is the statements
it was: `ciede_sycl` returns the same bits on all 178 frames of the fixtures
on an Arc A380, and `test_sycl_kernel_scratch` still finds its kernel free
of scratch memory (ADR-1395).

**The HIP primitives.** `core/src/feature/hip/integer_ciede/ciede_hip_math.h`
provides them. The exact pair operations are the new
`core/src/feature/ff_pair.h`: fp32 `+`, `-`, `*` and `/` are IEEE operations
under the strict FP list ([ADR-1407](1407-hip-strict-fp-every-kernel.md)), and
`fmaf()` is one rounding by definition. The root estimates the pair functions
correct are `sqrtf()`, `cbrtf()` and `expf(0.2f * logf(x))`; one Newton or
Halley step makes any estimate within 2^-16 of the root good to 2^-44. The
SYCL twin's fifth-root estimate is the device's `pow(x, 0.2f)`; on the
gfx1036 `powf()` gives the same pixels and takes 7 of 50 ms per 1920x1080
frame more.

**The HIP twin.** `ciede_score.hip` runs `pixel()` per thread and stores the
float at its raster position; nothing is reduced on the device. `ciede.c`'s
constants are evaluated by the compiler (`make_constants()` is `constexpr`),
one set per bit depth, and the two tables are constants of the module. The
host reads the plane back, adds it with `ciede_frame_sum()` and applies the
reference's `45. - 20. * log10(de00_sum / (w * h))`. `ciede_frame_sum()`
moves to `core/src/feature/ciede_frame_sum.h`, one definition for the CUDA,
SYCL and HIP hosts.

**What remains, measured.** A probe ran the kernel's statements on the
gfx1036 for every pixel of real frames and compared each value with a host
replay of the reference's fp64 statements (glibc 2.44), and each differing
value with a second replay whose `powf` is correctly rounded:

| Frames | Pixels | Differ | From glibc's `powf` | From the pair |
|---|---|---|---|---|
| BBB 3840x2160, 48 frames | 398 131 200 | 2 164 (at most 74 per frame) | 2 156 | 8 |
| Netflix 576x324 at 8 bit and as 10-bit 4:2:2, 96 frames | 17 915 904 | 3 | 3 | 0 |
| Netflix 576x324 at 10, 12 and 16 bit, 9 frames | 1 679 616 | 0 | 0 | 0 |
| Both 1920x1080 checkerboard pairs, 6 frames | 12 441 600 | 0 | 0 | 0 |
| Bright 16 bit 1920x1080, 2 frames | 4 147 200 | 27 | 27 | 0 |
| Noise 576x324 at four depths, 12 frames | 2 239 488 | 19 | 19 | 0 |
| Sparks 480x270, 5 frames | 648 000 | 1 | 1 | 0 |

2 214 of 437 million pixels differ. 2 206 of them are one float step off
because glibc's `powf` returns the other neighbouring float. The other 8,
all in the first twelve BBB frames, are one to nine float steps off: a pair
carries about 48 bits where fp64 has 53, one of the reference's intermediate
floats rounds the other way, and a cancellation later in the formula
amplifies that step. One float step of a pixel moves
`45 - 20 * log10(mean)` by at most
`8.7 * 2^-23 * (value / mean) / pixels`, which is 5.6e-12 at 576x324 for a
pixel at the mean. `scripts/ci/cross_backend_calibration.py` lists `ciede`:
`hip` at `1e-9` in `LIBM_TWINS`, the bound of the CUDA and SYCL twins; the
largest measured difference is 1.4e-11.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| The SYCL twin's fp32-pair statements from a shared header (this ADR) | One implementation of the pair evaluation for both twins; no fp64 math on the device; 2.7 times the frame time | 8 pixels in 437 million differ through the pair; the SYCL twin's headers change in the same pull request | Chosen |
| The CUDA twin's fp64 statements (the first version) | One implementation with the CUDA twin; no pixel differs through the device's arithmetic | 17 times the frame time on a gfx1036 (318 ms per 1920x1080 frame) | Not merged for that cost |
| A HIP copy of the SYCL headers | No SYCL file touched | Two copies of 800 lines of pair arithmetic to keep in step | The SYCL twin stayed bit-identical after the move, so the copy is not needed |
| The device's `powf(x, 0.2f)` as the fifth-root estimate, as on SYCL | The same primitive list as SYCL | 56.7 instead of 49.6 ms per 1920x1080 frame for the same pixels | `expf(0.2f * logf(x))` is accurate enough for the Halley step |
| Keep the fp32 kernel and its `5e-3` | 18.6 ms per 1920x1080 frame | 1.1e-5 from the CPU for reasons that are the twin's own | The direction is the CPU's result |
| Reduce on the device instead of reading the plane back | No 33 MB readback per 3840x2160 frame | The readback and the host's sum are 2 of 50 ms; an ordered reduction is four more kernels | Not where the time is |
| Port glibc's `powf` to the device | Would remove 2 206 of the 2 214 differing pixels | Ties the twin to one libm's algorithm; another libm rounds other arguments | As in ADR-1426 |

## Consequences

- **Positive**: measured on a gfx1036 at `--precision max`, 115 of 178 frames
  are identical to `--backend cpu` and the rest within 1.4e-11, where no frame
  matched and the distance was up to 1.1e-5. Netflix 576x324 at 8 bits 47 of
  48 (6.9e-13 on the other); its 10-, 12- and 16-bit versions and both
  1920x1080 checkerboard pairs identical on every frame; BBB 3840x2160 within
  1.4e-11 on 48 frames, none identical; full-range noise within 4.6e-12.
  These are the CUDA and SYCL twins' figures.
- **Positive**: the gate tolerance of the HIP cell goes from `5e-3` to `1e-9`.
- **Positive**: the pair evaluation of `get_lab_color()` and `ciede2000()`
  exists once for the SYCL and HIP twins, and a HIP build checks it without
  a device (`test_hip_ciede_math`).
- **Negative**: the twin takes 2.7 times as long. Steady state inside one
  process, medians of three interleaved pairs of runs against
  `origin/master` 4cef2baa7: 18.6 ms before and 49.6 ms after at 1920x1080,
  75.6 and 210.1 ms at 3840x2160. Where it goes, from kernels cut short, at
  1920x1080 and 3840x2160: the two L\*a\*b\* conversions 21.9 and 86.8 ms,
  the colour difference 25.5 and 113.5 ms, the upload, the launch, the
  readback of one float per pixel and the host's sum 2.2 and 9.8 ms. The CPU
  extractor takes 136 ms per 3840x2160 frame on sixteen threads, so at that
  size the twin is slower than the CPU on this integrated GPU.
  `T-HIP-CIEDE-EXACT-THROUGHPUT-2026-10-02`.
- **Negative**: one float per pixel of device memory and of pinned host
  memory (33 MB each at 3840x2160).
- **Negative**: stored `ciede_hip` outputs change by up to 1.1e-5.
- **Neutral / follow-ups**:
  - The `1e-9` holds from 576x324 up (ADR-1426).
    `test_hip_ciede_parity` uses `1e-8` at 256x144 through
    `ciede_twin_parity.h`.
  - The device's values equal a host replay of the pair statements on all but
    one of the 437 million pixels (the host's `cbrtf`, `expf` and `logf` give
    another estimate there), so the device arithmetic itself is the header's.
  - `ciede_metal` is the last twin with the fp32 formulation; the shared
    headers need fp32 with FMA, correctly rounded division and square root,
    and no contraction.
  - Guards: `test_hip_ciede_parity` and `_large` (the ten cases of
    `ciede_twin_parity.h`; the first fails on the old twin by 4.2e-7),
    `test_hip_ciede_math` (the pair functions against the host's
    extended-precision math library and the pixel against the fp64
    statements, on the HIP primitives, no device),
    `test_hip_ciede_exact_contract.py` (eleven planted regressions, no
    device), and for the shared headers `test_sycl_ciede_exact_contract.py`
    and `test_sycl_ciede_math` in a SYCL build.

## References

- `req` (coordinator brief for the HIP lane, 2026-10-02): "`ciede_hip`: port `ciede_device.h` (ADR-1426), libm bound via `LIBM_TWINS` (measure the HIP device's functions; do not copy CUDA's 1e-9 unmeasured)."
- `req` (coordinator, 2026-10-02, after the fp64 version measured 17 times): "`ciede_hip` (#1790, draft at 17x): do NOT land the fp64 version. Port the fp32-pair evaluation the SYCL twin uses (`core/src/feature/sycl/sycl_ciede_math.h`, ADR-1436, merged as #1775; 3.1x on the A380, per-pixel values equal to the fp64 statements with correctly rounded powf). Move the arithmetic into a backend-neutral header if SYCL's is SYCL-specific only by its includes (one behaviour, one implementation: the SYCL twin then includes the shared header in the same PR, and its contract + parity tests must still pass on the A380"
- `req` (same message): "cost rule: above 20 % = explain + tuning row and land, above 3x = stop and report"
- [ADR-1426](1426-cuda-ciede-cpu-arithmetic.md),
  [ADR-1436](1436-sycl-ciede-cpu-arithmetic.md),
  [ADR-1407](1407-hip-strict-fp-every-kernel.md),
  [ADR-1395](1395-sycl-kernels-no-scratch.md),
  [ADR-1421](1421-rc3-rc8-candidate-map.md),
  [ADR-0214](0214-gpu-parity-ci-gate.md),
  [ADR-0220](0220-sycl-fp64-fallback.md),
  [ADR-0024](0024-netflix-golden-preserved.md).
- [Research-1436](../research/1436-sycl-ciede-fp32-pairs.md),
  [Research-1426](../research/1426-cuda-ciede-cpu-arithmetic.md).
- `docs/state.md`: `T-GPU-CIEDE-CPU-ARITHMETIC-2026-10-01`,
  `T-HIP-CIEDE-FP32-ARITHMETIC-2026-10-02`,
  `T-HIP-CIEDE-EXACT-THROUGHPUT-2026-10-02`.
