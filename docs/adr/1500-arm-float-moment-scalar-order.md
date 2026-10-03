<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1500: The NEON and SVE2 `float_moment` kernels add in the scalar's order and return its bits on every input and vector length

- **Status**: Accepted
- **Date**: 2026-10-03
- **Deciders**: lusoris
- **Tags**: `simd`, `arm64`, `neon`, `sve2`, `numerics`, `float-moment`, `testing`, `rc3`, `fork-local`
- **Supersedes**: the tolerance contract (`MOMENT_REL_TOL = 1e-7`) of [ADR-0179](0179-float-moment-simd.md), [ADR-0584](0584-moment-sve2-port.md) and [ADR-0987](0987-avx512-float-moment.md) for every `float_moment` SIMD kernel. Their kernels and dispatch stand.

## Context

`moment.c::compute_1st_moment()` and `compute_2nd_moment()` add every sample,
or its float square, into one `double` in raster order. On a 16-bit picture
(`picture_copy()` divides the samples by 256) a square is an integer number
of units of 2^-16 below 2^32, so the second moment's running sum passes 2^53
units on frames of more than 2^21 pixels with bright content. From there on
the `double` rounds on every add whose term has bits below its last place,
and the result depends on the order of the adds.

The x86 AVX2 and AVX-512 kernels square in vectors and add the lanes into
one `double` one after the other, the scalar's order. The aarch64 kernels did
not: `compute_*_moment_neon()` added into two `float64x2_t` accumulators
across the whole frame and reduced them at the end, and
`compute_*_moment_sve2()` reduced each row's vector sum into the running sum,
with a grouping set by the vector length. `T-ARM-MOMENT-NEON-SVE2-SUM-ORDER-2026-10-03`
recorded the gap for RC7; the maintainer moved it to RC3 and asked for the
scalar's bits on every input (References).

Measured under `qemu-aarch64` 11.1.1 with the new `test_moment_simd` cases
against the kernels of `6a3c26270`:

| Frame | NEON | SVE2 (128 / 256 / 512 / 2048 bits) |
|---|---|---|
| 4096x2048, sum just below 2^53 units (control) | equal | equal |
| 4096x2049, sum 2^53 units, then 4096 single units | 16376.00390435103 vs scalar 16376.003904343583 | same as NEON at every length |
| 3841x2160 bright and dark 16-bit noise, sum about 2^54.2 units | 37917.601966364906 vs 37917.601964216701 | 37917.60196636492 at every length |
| 16x1, one sample of 1 and fifteen of 1.5 * 2^-53 (first moment) | 0.062500000000000167 vs 0.062500000000000208 | ...18 at 128 bits, ...167 at 256, 512, 2048 |

Bright noise alone does not show the gap: a square above 2^31 units is a
float with a multiple of 128 units, which a `double` below 2^60 units adds
exactly. The dark samples (odd, below 4096) carry the low bits.

The first moment of a `picture_copy()` picture is exact in any order (sums
below 2^46 units of 2^-8), but the kernels take any float array, and the last
row of the table shows the lane grouping is not the scalar's sum on one.

## Decision

The NEON and SVE2 kernels add in the scalar's order, as `moment_avx2.c` does:

- NEON: load four samples, square them with `vmulq_f32` for the second
  moment, store the four floats and add them into the running `double` one
  after the other (`moment_add4()`); the row tail adds sample by sample, the
  square formed in `float`.
- SVE2: load `svcntw()` samples under `svwhilelt_b32(j, w)`, square them with
  `svmul_f32_x` for the second moment, store the active lanes into a
  64-float buffer (the widest SVE vector is 2048 bits) and add the first
  `svcntp_b32` of them in lane order (`moment_add_active()`). The active lanes
  of a `whilelt` predicate are the first ones, in order, so the adds are
  row[j], row[j + 1], ... at every vector length. No lane is widened or
  reduced inside a vector.
- Both moments, so every kernel returns the scalar's bits on every float
  array, not only on `picture_copy()` values.

`test_moment_simd` asserts `==` against the scalar functions for every
kernel the build has (AVX2, AVX-512, NEON, SVE2) on random frames, tail
widths 1 to 15, the four frames of the table and the 2^53 boundary control.
Every kernel and every frame is run and reported even after a failure.

Found on the way and fixed in the same change because it is the same line in
two tests: `test_moment_simd` and `test_iqa_convolve` asked
`vmaf_get_cpu_flags()`, which reads 0 until `vmaf_init_cpu()` has run, and
neither calls it. The SVE2 moment cases and the NEON convolution cases were
skipped on every aarch64 processor. Both now ask the processor
(`vmaf_get_cpu_flags_arm()`); the 13 NEON convolution cases pass under
`qemu-aarch64`.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Add in the scalar's order (this ADR) | The scalar's bits on every input and every vector length by construction; the same form as the x86 kernels; small, reviewable kernels | The adds are one dependent chain, as in the scalar loop, so the kernels lose the speed of lane accumulators | Chosen |
| Exact integer sums rounded the way `compute_2nd_moment()` rounds (the GPU twins' arithmetic, ADR-1497) | Lane-parallel adds | The kernels take floats without a scale; an integer sum is exact only on `picture_copy()` values and needs the bit depth; the scalar's per-add rounding past 2^53 needs ADR-1497's plan, increments and walk, far more code than the kernel it replaces | Not the scalar's bits on every float array, and a second implementation of a loop that the scalar code already is |
| Return the scalar path above 2^21 pixels at 16 bits | One branch | The kernels do not know the bit depth; a threshold on the frame does not cover arbitrary floats; the first moment is still grouped | Partial |
| Change the second moment only | Keeps the vector first moment | The first moment is not the scalar's sum on arbitrary floats (table) | The contract is the scalar's bits on every input |
| Keep the 1e-7 tolerance | No change | Not the scalar's bits; the aarch64 CPU extractor and the x86 one return different second moments on 16-bit 4K frames | The maintainer decided against it (References) |

## Consequences

- **Positive**: the aarch64 CPU `float_moment` returns the x86 and scalar
  bits on every input; the SVE2 kernel is independent of the vector length;
  the CUDA, SYCL and HIP twins (ADR-1497) and the aarch64 CPU agree past
  2^53. The SVE2 moment cases and the NEON convolution cases run under
  emulation for the first time.
- **Negative**: the NEON and SVE2 kernels now run at about the scalar loop's
  speed (one dependent add per sample, as on x86). Not measured on aarch64
  hardware; `T-ARM-MOMENT-SCALAR-ORDER-COST-2026-10-03` (RC8) holds the
  measurement and any tuning that keeps the bits.
- **Neutral**: x86 object code is unchanged (every object and library of an
  x86 build hashes the same before and after; only the test object differs).
  `make test-netflix-golden-arm64` passes (280 passed, 3 skipped).

Other aarch64 kernels that add floating-point values in lanes, checked for
the same gap:

- `float_psnr_neon.c`: two `float64x2_t` lanes per row of float squares; a
  term is below 2^32 units at 16 bits and a row has at most 2^15 of them, so
  every partial sum of a row is an integer below 2^47 units, exact in any
  order. The rows are added in order by the caller. No change.
- `float_adm_neon.c` (`float_adm_csf_den_scale_neon()`,
  `float_adm_sum_cube_neon()`): lane sums of float cubes, not the reference's
  order; built but not dispatched, tracked by
  `T-FLOAT-ADM-X86-SCALAR-STAGES-2026-10-02`. No change.
- `convolve_neon.c`, `speed_neon.c`: one lane per output or per sum, never a
  sum split over lanes. No change.
- Integer kernels (`psnr_neon.c`, `vif_neon.c`, `motion_v2_neon.c`,
  `adm_neon.c`, `psnr_hvs_neon.c`): 64-bit integer sums, exact in any order.
- There is no CPU integer `moment` extractor; the GPU `integer_moment_*`
  files are the `float_moment` twins.

## References

- `Q` (maintainer popup, 2026-10-03, NEON/SVE2 float_moment): "RC3, fix before rc.3 (Recommended)".
- `req` (coordinator brief, 2026-10-03): "The NEON and SVE2 `float_moment` paths return the scalar path's bits on every input".
- [ADR-1497](1497-float-moment-twins-cpu-sum-past-2-53.md): the GPU twins' form of the same sum.
- [ADR-1490](1490-rc3-rc9-candidate-map-cpu-capability.md): the emulated bit-exactness bar for every dispatch level.
- `docs/state.md`: `T-ARM-MOMENT-NEON-SVE2-SUM-ORDER-2026-10-03` (closed by this ADR), `T-ARM-MOMENT-SVE2-TEST-NEVER-RAN-2026-10-03` (closed by this ADR), `T-ARM-MOMENT-SCALAR-ORDER-COST-2026-10-03` (opened).
