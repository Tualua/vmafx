<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1426: `ciede_cuda` computes the CPU's arithmetic and adds in the CPU's order; what remains is the math library, and the gate bounds it

- **Status**: Accepted
- **Date**: 2026-10-01
- **Deciders**: lusoris
- **Tags**: `cuda`, `gpu-parity`, `numerics`, `ciede`, `testing`, `ci`, `rc3`, `fork-local`

## Context

`ciede_cuda` matched the CPU `ciede` extractor on no measured frame and was
up to 1.1e-5 from it (Netflix 576x324; 1.5e-6 at 3840x2160). The gate allows
it `5e-3` ([ADR-0187](0187-ciede-vulkan.md)), on the reasoning that a
float kernel full of transcendentals cannot do better.

It can, because the distance was not the transcendentals. `ciede.c` computes
in double and stores in float: `get_lab_color()` is fp64 up to the cube root,
`ciede2000()` evaluates almost every expression in fp64 (a double literal or
a libm call is part of it) and names the result `const float`. The kernel was
fp32 throughout, with float math functions and a rearranged formula (hue in
degrees, `7.787 t + 16/116` for the linear branch), and it added per warp and
per 16x16 block in fp32. Rewritten in the reference's types, the same device
agrees with the CPU to 1.4e-11
([Research-1426](../research/1426-cuda-ciede-cpu-arithmetic.md)).

What is left is the math library, and it cannot be removed the way the
arithmetic was. The CPU calls glibc, the device CUDA's functions:

- glibc's `powf` is not correctly rounded: of the arguments `get_r_sub_t()`
  passes, 0.07 % of `powf(x, 7)` and 0.16 % of `powf(x, 2)` return the other
  neighbouring float. On one 3840x2160 frame that changes 38 of 8 294 400
  per-pixel values by one float step.
- the fp64 functions (`pow`, `atan2`, `sin`, `cos`, `exp`) differ between the
  two libraries in their last place (CUDA documents 1 to 2 units for them).
  Each result is rounded to float a few operations later, so a difference
  survives only when it straddles a float rounding boundary: one pixel on
  that frame.

The maintainer's direction for the CUDA twins in this lane is results first,
bit for bit, speed afterwards; where the CPU's own math library is the
obstacle, name what differs and get everything else identical.

## Decision

We will make `ciede_cuda` evaluate `ciede.c`'s arithmetic in its types and add
the per-pixel values in its order, and give the gate a tolerance that
reflects what is left.

**The reference's expressions, written once.**
`core/src/feature/cuda/integer_ciede/ciede_device.h` is `get_lab_color()`,
`ciede2000()` and their helpers, statement for statement: double where the
reference computes in double, float where it stores in float, every
float-to-double promotion that C applies to a libm argument written out
(the kernel is C++, which would pick the float overload). `pow(x, 2)` of a
float is the exact fp64 product. The two `powf` calls of `get_r_sub_t()` are
the correctly rounded value on the device and glibc's on the host. The host
compiles the same header for a test.

**No reduction on the device.** `extract()` adds every pixel's value into one
double, row after row. In a large frame those additions round, so the kernel
stores the float of every pixel at its raster position, the plane is read
back, and `ciede_frame_sum()` adds it in that order.

**A gate tolerance for twins that differ only in their math library.**
`scripts/ci/cross_backend_calibration.py` gets `LIBM_TWINS`, with
`ciede`: `cuda` at `1e-9`. A cell whose sides are the CPU or a listed twin
takes that tolerance (source `libm:ADR-1426`) and runs at `--precision max`.
`ciede_cuda` is not listed in `EXACT_TWINS`: it is not bit-identical.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Keep the fp32 kernel and its `5e-3` | Fast (2.8 ms per 4K frame) | 1.1e-5 from the CPU for reasons that are the twin's own | The direction is the CPU's result |
| Port glibc's `powf` (table-driven fp64 evaluation) to the device | Would remove 38 of the 39 differing pixels per 4K frame | Ties the twin to one libm's algorithm (another libm, or a glibc built with FMA, rounds a few arguments differently); the fp64 functions would still differ, so the cell could still not be `0` | A candidate recorded in `T-CUDA-CIEDE-LIBM-RESIDUAL-2026-10-01`; this ADR stops at the arithmetic |
| CUDA's `powf` for the two float powers | The literal translation | Its documented maximum error is 4 units in the last place; it would differ from glibc far more often than the correctly rounded value does | The correctly rounded value equals glibc's in more than 99.8 % of the calls |
| Correctly rounded fp64 functions on the device (double-double) | Fewer straddling pixels | glibc is not correctly rounded either, so the pixels would not go to zero; several times the fp64 work | One pixel per 4K frame is not worth it |
| Per-block fp32 sums, blocks added in double (the old reduction) | No read-back of 33 MB | 1.4e-9 at 3840x2160 on its own, a hundred times the math-library residual | The sum is the CPU's or it is not |
| Change the CPU to float math or to correctly rounded calls | Twin and CPU could match exactly | Moves the Netflix golden values | [ADR-0024](0024-netflix-golden-preserved.md) |

## Consequences

- **Positive**: measured on an RTX 4090 at `--precision max` against master
  `5c8b9e9c7`: 62 of 113 frames identical to `--backend cpu` and the rest
  within 1.4e-11, where no frame matched and the distance was 1.1e-5.
  Netflix 576x324 at 8 bits 47 of 48 (6.9e-13 on the other); its 10-, 12-
  and 16-bit versions and both 1920x1080 checkerboard pairs identical on
  every frame; BBB 3840x2160 within 1.4e-11 on 200 frames, none identical.
- **Positive**: the gate tolerance for the CUDA cell goes from `5e-3` to
  `1e-9`.
- **Negative**: a run of the twin alone takes 32.7 ms per 3840x2160 frame
  instead of 2.8 ms (seven alternating pairs of 50 frames, paired difference
  +29.96 ms, quartiles +28.81 to +32.51, host load average 14 to 19), and
  0.74 ms instead of 0.34 ms at 576x324. The kernel now runs about two dozen
  fp64 transcendentals per pixel on a device whose fp64 rate is a fraction of
  its fp32 rate, and the host adds 8.3 million values per frame. The CPU
  extractor takes 2 644 ms per 4K frame on one thread and 222 ms on sixteen.
  `T-CUDA-CIEDE-EXACT-THROUGHPUT-2026-10-01`.
- **Negative**: 33 MB of device memory and as much pinned host memory at
  3840x2160 (one float per pixel).
- **Negative**: stored `ciede_cuda` outputs change by up to 1.1e-5.
- **Neutral / follow-ups**:
  - The residual is recorded with its cause in
    `T-CUDA-CIEDE-LIBM-RESIDUAL-2026-10-01`. It is a property of the pair of
    math libraries: a host whose libm rounds `powf` differently has a
    different set of straddling pixels, of the same size.
  - The `1e-9` holds from 576x324 up. One straddling pixel moves the score by
    at most `8.7 * 2^-23 * (value / mean) / pixels`, so a frame of a few
    thousand pixels can exceed it; `test_cuda_ciede_parity` uses `1e-8` at
    256x144.
  - The contract mirrors `ciede.c`. A change to `get_lab_color()`,
    `ciede2000()` or the order of `extract()`'s sum changes
    `ciede_device.h` in the same PR. `test_ciede_device_math` replays the
    header against the CPU extractor on the host and fails when they diverge;
    `test_cuda_ciede_exact_contract.py` pins the design at source level.
  - `ciede_sycl`, `ciede_hip` and `ciede_metal` keep the fp32 formulation and
    the `5e-3` tolerance: `T-GPU-CIEDE-CPU-ARITHMETIC-2026-10-01`.

## References

- `req` (maintainer brief, 2026-10-01): "results before speed; a twin
  reproduces the CPU bit for bit, and tuning comes afterwards."
- `req` (maintainer brief, 2026-10-01): "Where the CPU's own libm call is the
  obstacle [...], say exactly which outputs and frames, and get everything
  else identical."
- [ADR-1403](1403-cuda-strict-fp-every-kernel.md),
  [ADR-1380](1380-cuda-speed-device-resident-pipeline.md),
  [ADR-0762](0762-cuda-ciede-ldg.md),
  [ADR-0187](0187-ciede-vulkan.md),
  [ADR-0214](0214-gpu-parity-ci-gate.md),
  [ADR-0024](0024-netflix-golden-preserved.md).
- [Research-1426](../research/1426-cuda-ciede-cpu-arithmetic.md).
- NVIDIA, CUDA Programming Guide, appendix "Mathematical Functions"
  (<https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/mathematical-functions.html>,
  read 2026-10-01): maximum error 4 ULP for `powf`, 2 ULP for `pow`, `atan2`,
  `sin` and `cos`, 1 ULP for `exp`, 0 for `sqrt`.
