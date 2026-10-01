<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1443: `integer_ssim_sycl` computes the CPU's fp64 term in 64-bit integers and adds in the CPU's order; it returns the CPU's `ssim` bit for bit

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `sycl`, `gpu-parity`, `numerics`, `ssim`, `testing`, `ci`, `rc3`, `fork-local`

## Context

`integer_ssim_sycl` is the SYCL twin of the fixed-point `ssim` extractor
(`integer_ssim.c`). Its int64 window moments equal the CPU's. On an Arc A380
at `--precision max` its score matched the CPU on no frame of real content
and was up to 3.1e-7 from it (BBB 3840x2160; 6.9e-9 on the Netflix 576x324
pair, 1.1e-7 on the 1920x1080 checkerboards). On 16-bit input the run failed:
`invalid ratio ... numerator=inf`.

Two things differed from the reference.

`ssim_reduce_row_range()` forms each pixel's term in fp64: the stabilisers
`c1` and `c2`, three products of two moments, and one expression with nine
sums, three products and a quotient. A SYCL kernel has no fp64 type
([ADR-0220](0220-sycl-fp64-fallback.md); one fp64 instruction blocks the
translation unit on Arc A-series), so the twin formed the term in fp32. That
is where most of the difference came from, and fp32 overflows on the squares
of 16-bit moments.

`calc_ssim()` adds every term into one `double`, left to right and top to
bottom. The twin added fp32 partial sums per 16x8 work-group. A sum of
doubles is its order: [ADR-1424](1424-cuda-ssim-cpu-frame-sum.md) measured
1.1e-11 from the order alone on the CUDA twin, whose term already was the
CPU's.

The direction for the GPU twins is results first, bit for bit, speed
afterwards. ADR-1424 left the SYCL twin open
(`T-GPU-SSIM-FRAME-SUM-ORDER-2026-10-01`) and noted that it needed more than
the order.

## Decision

We will compute the reference's fp64 term on the device without an fp64 type,
store it unreduced, and add the terms on the host in the reference's order.

**fp64 operations in integers.** `core/src/feature/sycl/sycl_soft_signed.h`
holds an fp64 value as a sign, a 53-bit significand and an exponent in
integers and has the sum, difference, product and quotient of two such values
and the conversion from a 64-bit integer, each rounded to nearest, ties to
even, as the fp64 operation is. It builds on
`sycl_soft_double.h` ([ADR-1432](1432-sycl-integer-vif-exact-gain.md)), which
has the positive values; this header adds the sign, zero and the difference
with its cancellation.

**The term, operation for operation.**
`core/src/feature/sycl/sycl_integer_ssim_math.h::term_bits()` runs the
reference's expression in the reference's order on those values and returns
the bit pattern of the resulting `double`. The stabilisers' common factor
`sm * sm * SSIM_K1` is formed on the host from the reference's own expression
and reaches the kernel by value.

**Three shortcuts, each exact.**

- A window inside the frame has the weight 2^16, and a product with a power
  of two is exact: `times_weight()` adds 16 to the exponent. Only a window
  the frame's edge truncates takes the multiplication. `c1`, `c2` and
  `m.w * (2 * mxy + c1)` are five such products.
- When all six products of two moments are below 2^52, each is an fp64 value
  as it stands and so is every sum and difference the reference forms from
  them before a stabiliser is added (an integer below 2^53). That is every
  window at 8 and 10 bits. `product_sums_exact()` then does integer
  arithmetic and four conversions that do not round, where
  `product_sums_rounded()` rounds six products and five sums.
- The quotient comes from three steps of a long division in radix 2^19
  (`soft_div_digits()`). A digit is estimated by an fp32 division of the top
  24 bits of remainder and divisor and corrected from the exact integer
  remainder, two units each way, so the accuracy of the device's fp32
  division does not enter the result.

**No reduction on the device.** The kernel stores the 64-bit pattern of every
pixel's term at its raster position. The host reads the plane back and adds
it into one `double` in index order, which is `calc_ssim()`'s order.

**The weight is not a plane.** A window's weight is the product of its two
tap sums. The kernel computes it from the pixel's position, and the frame's
weight sum is the product of the two line sums, an integer the host forms at
init. The horizontal pass writes five int64 planes where it wrote six, so the
plane of terms does not add device memory.

**Kernel shape.** SIMD-16 sub-groups with the 256-entry register file
(`VmafSyclKernelShape<16, 256>`). With the default register file SIMD-16
takes 3 KiB of private memory and spills 1.2 KiB; SIMD-32 spills with
either. Both are scratch memory, which returns wrong values on Arc A-series
under xe ([ADR-1395](1395-sycl-kernels-no-scratch.md)): the first build
scored 0.64 off on the 1920x1080 fixtures. The per-pixel function is flattened into the
kernel and the headers' functions are always inlined, because a call left in
a kernel takes scratch memory for its frame.

**The gate.** `scripts/ci/exact_twins.d/ssim.sycl` declares the twin exact:
the CPU ↔ SYCL cell of `ssim` is compared with tolerance 0
([ADR-1428](1428-exact-twins-fragments.md)).

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| The reference's fp64 operations in 64-bit integers, terms unreduced, host sum in raster order (this ADR) | The CPU's score bit for bit at every bit depth; fp64-free; scratch-free; 16-bit input works | 31.9 ms per 4K frame instead of 17.8; 66 MB of pinned host memory at 4K | Chosen |
| Keep the fp32 term and the per-group sums | 17.8 ms per 4K frame | 3.1e-7 from the CPU; fails on 16-bit input | The direction is the CPU's result |
| fp32 pairs for the term (`sycl_exact_fp.h`'s `Ff`, as `float_ssim_sycl` and `float_ms_ssim_sycl` use) | Cheaper than integer arithmetic per operation | A pair holds about 48 bits; the term is a 53-bit `double` that goes into a sum of doubles, so a value near it is not enough: stored as a `float` the exact term alone leaves 3.7e-10 | Cannot return the term's bits |
| An fp32 or pair estimate first, the integer operations only for a term the estimate does not decide ([ADR-1422](1422-sycl-float-vif-cpu-arithmetic.md)'s zones) | Cheap for most pixels where the result is a rounded float | Here the result is all 53 bits of every term; nothing is decided by fewer | No zone to test |
| The same arithmetic with general operations only: every product by the weight multiplied, every sum rounded, a 56-step division (the first version of this change) | Less code, one path | 40.9 ms per 4K frame; identical scores | The three shortcuts are exact and give 31.9 ms |
| Read the six moment planes back and form the term on the host | The CPU's own code | 398 MB per 4K frame over the bus and 8.3 million terms on one host thread: the CPU extractor's cost | Not a GPU twin |
| Add the terms on the device in the CPU's order | No read-back | One dependent fp64 add per pixel on one device thread, in integers | Orders of magnitude slower than the host |
| The per-binade integer sum of `ordered_sum.h` ([ADR-1433](1433-cuda-ssimulacra2-cpu-sum-order.md)) on the device | Removes the read-back and the host adds (8.6 ms) | It requires non-negative terms; an SSIM term is negative where a window's covariance is, and a chunk with one falls back to its terms, which then have to be read back anyway | The tuning candidate, `T-SYCL-SSIM-EXACT-THROUGHPUT-2026-10-02`; this ADR takes the simple exact form first |
| Change the CPU to float | Twins could match cheaply | Moves the Netflix golden values | [ADR-0024](0024-netflix-golden-preserved.md) |

## Consequences

- **Positive**: measured on an Arc A380 (xe, Level Zero, icpx 2026.0) at
  `--precision max` against a GCC build of the CPU extractor, the score of
  every frame equals `--backend cpu`: Netflix 576x324 at 8 bits (48 frames),
  at 10, 12 and 16 bits and as 4:2:2 10-bit (3 frames each), both 1920x1080
  checkerboard pairs (3 frames each) and BBB 3840x2160 (200 frames), 266 of
  266; before, 0 of the 263 that ran, up to 3.1e-7 (2.0e-4 in dB). The CPU
  extractor of the icx build gives the same scores. The gate reports 0 on all
  four fixtures.
- **Positive**: 16-bit input works. The fp32 term overflowed and the run
  stopped with `invalid ratio`.
- **Positive**: what each cause contributed, from the new twin with one piece
  put back (Netflix, 1 px checkerboard, 10 px checkerboard, BBB 20 frames):
  the term in fp32 6.6e-9, 9.4e-8, 1.1e-7, 3.1e-7; fp32 partial sums per 16x8
  block 1.3e-8, 6.8e-8, 5.6e-8, 1.5e-8; the exact term stored as a float
  9.4e-11, 3.2e-9, 3.3e-9, 3.7e-10; the order alone (double sums per block)
  2.3e-14, 1.6e-12, 1.1e-11, 5.6e-13.
- **Positive**: the arithmetic is checked apart from the device. Every
  operation equals the host's fp64 operation on 400 000 operand pairs each
  (cancelling, ties, integers to 2^64, zeros), and the term equals the
  reference's expression on 300 000 windows per bit depth, on the host and in
  a kernel (`test_sycl_integer_ssim_math`).
- **Negative**: a run of the twin alone takes 31.9 ms per 3840x2160 frame
  instead of 17.8 ms (medians of 11 runs of 50 frames, host load average 9 to
  11; the `float_psnr` control read 3.28 ms on both builds), and 0.78 ms
  instead of 0.45 ms at 576x324. From kernels and host code with a stage
  removed: 7.2 ms the term's integer arithmetic, 5.8 ms the read-back of 66
  MB, 2.8 ms the host's 8.3 million additions, 16.4 ms the uploads and the
  two moment passes. The CPU extractor of the GCC build takes 111 ms.
  `T-SYCL-SSIM-EXACT-THROUGHPUT-2026-10-02`.
- **Negative**: 66 MB more pinned host memory at 3840x2160 (one 64-bit
  pattern per pixel). Device memory is unchanged.
- **Negative**: stored `integer_ssim_sycl` outputs change by up to 3.1e-7.
- **Neutral / follow-ups**:
  - With `enable_db` the twin equals the CPU extractor of its own build on
    every frame. Against a GCC build 10 of 266 frames differ by at most
    3.6e-15: the dB value is `-10 * log10(1 - ssim)` on the host, and an icx
    build links Intel's `log10` where a GCC build links glibc's
    (`T-ICX-LIBIMF-HOST-MATH-2026-10-01`). The CPU extractor of an icx build
    differs from a GCC build in the same way.
  - A zero has no sign in `sycl_soft_signed.h`. The term goes into a sum,
    where the sign of a zero changes nothing. A caller that needs `-0` has to
    add it.
  - The header mirrors `ssim_reduce_row_range()`. A change to that
    expression changes `sycl_integer_ssim_math.h` in the same PR.
    `test_sycl_ssim_exact_contract.py` fails when the lines move.
  - `core/test/ssim_twin_parity.h` holds the parity cases for any backend;
    `test_cuda_ssim_parity.c` and `test_hip_ssim_parity.c` have theirs inline
    and can move to it.
  - `integer_ssim_metal` keeps `float` partial sums per work-group
    (`T-GPU-SSIM-FRAME-SUM-ORDER-2026-10-01`). Metal has no fp64 either and
    can take these two headers.

## References

- `req` (maintainer brief for the SYCL exactness lane, 2026-10-01): "results before speed; a twin reproduces the CPU bit for bit, tuning comes afterwards".
- [ADR-1424](1424-cuda-ssim-cpu-frame-sum.md) (the CUDA twin, the gate's
  `ssim` feature), [ADR-1432](1432-sycl-integer-vif-exact-gain.md) (the
  positive soft-fp64 operations), [ADR-1433](1433-cuda-ssimulacra2-cpu-sum-order.md),
  [ADR-1428](1428-exact-twins-fragments.md),
  [ADR-1395](1395-sycl-kernels-no-scratch.md),
  [ADR-0220](0220-sycl-fp64-fallback.md),
  [ADR-0564](0564-integer-ssim-gpu-real-kernels.md),
  [ADR-0024](0024-netflix-golden-preserved.md).
- [Research-1443](../research/1443-sycl-ssim-soft-fp64.md).
- `docs/state.md`: `T-GPU-SSIM-FRAME-SUM-ORDER-2026-10-01` (the SYCL part
  closed by this decision), `T-SYCL-SSIM-FP32-TERM-2026-10-02`,
  `T-SYCL-SSIM-EXACT-THROUGHPUT-2026-10-02`.
