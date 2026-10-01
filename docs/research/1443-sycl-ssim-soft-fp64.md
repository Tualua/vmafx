<!-- markdownlint-disable MD013 MD060 -->
# Research-1443: fixed-point ssim on SYCL without fp64 — where the fp32 twin differed, the fp64 term in 64-bit integers, and what each part costs on an Arc A380

- **Status**: Active
- **Workstream**: [ADR-1443](../adr/1443-sycl-ssim-cpu-arithmetic.md), [ADR-1424](../adr/1424-cuda-ssim-cpu-frame-sum.md), [ADR-1432](../adr/1432-sycl-integer-vif-exact-gain.md), [ADR-0220](../adr/0220-sycl-fp64-fallback.md), [ADR-1395](../adr/1395-sycl-kernels-no-scratch.md)
- **Last updated**: 2026-10-02

## Question

[Research-1424](1424-cuda-ssim-cpu-frame-sum.md) found that the CUDA twin of
the fixed-point `ssim` extractor differed from the CPU only in the order of
its frame sum. The SYCL twin has no fp64 type and formed the term in fp32.
How large is each difference on the SYCL twin, can the reference's fp64 term
be produced on a device without fp64 so that the sum of the terms is the
CPU's, and what does that cost?

## Sources

- CPU: `core/src/feature/integer_ssim.c` (`ssim_accumulate_row()`,
  `ssim_reduce_row_range()`, `calc_ssim()`).
- SYCL: `core/src/feature/sycl/integer_ssim_sycl.cpp` at master `953cf6ea6`
  (before) and on `fix/sycl-ssim-cpu-arithmetic` (after),
  `core/src/feature/sycl/sycl_soft_double.h`,
  `core/src/feature/sycl/sycl_soft_signed.h` (new),
  `core/src/feature/sycl/sycl_integer_ssim_math.h` (new).
- Host: Arc A380 (xe driver, Level Zero), icpx 2026.0.0, gcc 16.2.1, Linux
  7.2.8, Ryzen 9 9950X3D. `meson setup build-sycl core -Denable_sycl=true
  -Denable_cuda=false -Dsycl_icpx_aot_targets= --buildtype=release
  -Db_lto=false`; the CPU reference is a GCC build.
- Fixtures, `--precision max`: the Netflix pair `src01_hrc00/01_576x324` at 8
  bits (48 frames), its 10-, 12- and 16-bit versions and the 4:2:2 10-bit
  version (3 frames each), the checkerboard pairs
  `checkerboard_1920_1080_10_3_0_0` against `_1_0` and `_10_0` (3 frames
  each), and BBB 3840x2160 (200 frames).

## Findings

### 1. The differences, one at a time

Each row puts one property of the old twin back into the new one. Largest
absolute difference of `ssim` against `--backend cpu`; BBB over its first 20
frames.

| Property put back | Netflix 576x324 | Checkerboard 1 px | Checkerboard 10 px | BBB 3840x2160 |
|---|---:|---:|---:|---:|
| none (the new twin) | 0 | 0 | 0 | 0 |
| the term computed in fp32, added in the CPU's order | 6.6e-9 | 9.4e-8 | 1.1e-7 | 3.1e-7 |
| the exact term, fp32 partial sums per 16x8 block | 1.3e-8 | 6.8e-8 | 5.6e-8 | 1.5e-8 |
| the exact term stored as a float, added in the CPU's order | 9.4e-11 | 3.2e-9 | 3.3e-9 | 3.7e-10 |
| the exact term, fp64 sums per 16x8 block (the order alone) | 2.3e-14 | 1.6e-12 | 1.1e-11 | 5.6e-13 |
| the old twin | 6.9e-9 | 1.0e-7 | 1.1e-7 | 3.1e-7 |

The fp32 term dominates on real content. The last variant is the CUDA twin's
state before ADR-1424 and gives that ADR's figures. The third row is why an
approximation of the term is not enough: a term correct to 24 bits still
leaves 3.7e-10, and the term has to be the `double`.

At 16 bits the old twin did not return a score. Its fp32 denominator
`(mx2 + my2 + c1) * (variances + c2)` is near 2^64 times 2^64 and overflows;
the run stopped with `invalid ratio at frame 0 (numerator=inf)`.

### 2. What the term needs

The moments are integers and not negative: `mux`, `muy` below 2^32 and `x2`,
`xy`, `y2` below 2^48 at 16 bits, `w` at most 2^16. So each product of two
converted integers in the reference (`m.mux * (double)m.mux`, `m.xy * w_d`,
...) is one rounding of an integer that fits 64 bits. The rest is 9 sums and
differences, 5 products with the weight, 2 products of two sums and 1
quotient, each rounded to nearest. The covariance `m.xy * w_d - mxy` can be
negative and so can the term; the variances cannot.

`sycl_soft_signed.h` does those operations on a sign, a 53-bit significand
and an exponent:

- **Sum and difference.** The smaller operand is aligned under the larger
  with three guard bits, the bits shifted out kept as one sticky bit. For a
  difference: with the exponents at most one apart nothing is shifted out and
  the result is exact however many leading bits cancel; further apart the
  result keeps at least half of the larger operand, is normalised by one
  place at most, and the sticky bit stays below the rounding bit.
- **Product.** `sycl_soft_double.h`'s 106-bit product in 32-bit limbs
  (`sycl::mul_hi()` on 64-bit operands returns wrong values on the A380).
- **Quotient.** Three digits in radix 2^19. A digit is estimated as
  `(float)(rem >> 29) / (float)(den >> 29) * 2^19`: both operands are exact
  in fp32, the truncation moves the quotient by less than 2^-22 and a
  division accurate to 2.5 units in the last place by less than 2^-22 more,
  so the estimate is within one of the digit. The remainder
  `rem * 2^19 - digit * den` lies within three divisors of zero, so its low
  64 bits are its value, and two corrections each way fix the digit. The
  result does not depend on how the device rounds the fp32 division.

### 3. The shortcuts and their effect

Through the `vmaf` tool on the A380, 3840x2160, one twin alone, medians of 3
to 11 runs of 50 frames:

| Version | ms per frame |
|---|---:|
| the old twin | 17.8 |
| general operations only, 56-step division, SIMD-16, 256-entry register file | 40.9 |
| the same at SIMD-8, default register file | 45.1 |
| radix-2^19 division and the power-of-two weight | 36.1 |
| and the integer path for products below 2^52 (this change) | 31.9 |

Each shortcut is exact, not a faster approximation:

- A window inside the frame has the weight 256 * 256 = 2^16. A product with
  a power of two only moves the exponent. Windows the edge truncates (four
  rows and columns each side) take the general product.
- With every product below 2^52, `2 * mxy`, `mx2 + my2`,
  `2 * (m.xy * w_d - mxy)` and `m.x2 * w_d - mx2 + m.y2 * w_d - my2` are
  integers below 2^53 and the reference rounds none of them. At 8 bits the
  products are below 2^48 and at 10 bits below 2^52, always. At 12 bits it
  depends on the window (`mux` up to 2^28), and at 16 bits it is rare; those
  take the rounding path. `test_sycl_integer_ssim_math` requires the 12-bit
  samples to reach both.

### 4. Where the 31.9 ms go

From builds with a stage removed (SIMD-16, 256-entry register file):

| Stage | ms |
|---|---:|
| uploads, the horizontal moment pass, the vertical moments, the store | 16.4 |
| the term's integer arithmetic | 7.2 |
| read-back of the 66 MB plane of terms | 5.8 |
| the host's 8.3 million additions | 2.8 |

The CPU extractor of the GCC build takes 111 ms for the frame.

### 5. Kernel shapes

| Sub-group size | Register file | Scratch | ms per 4K frame |
|---|---|---|---:|
| 16 | 256 | none | 31.9 |
| 8 | default | none | 33.9 |
| 16 | default | 3 KiB private, 1.2 KiB spilled | (30.8, wrong values) |
| 32 | 256 | spilled | (57.6, wrong values) |
| 32 | default | 6 KiB private, 8 KiB spilled | not run |

The shapes with scratch memory scored 0.64 off on the 1920x1080 fixtures and
correctly at 576x324, the behaviour ADR-1395 describes for xe.

### 6. Results

`ssim` against `--backend cpu` of a GCC build, identical frames:

| Fixture | Before | After |
|---|---|---|
| Netflix 576x324 8-bit, 48 frames | 0, 6.9e-9 | 48 |
| Netflix 10-bit, 3 frames | 0, 3.2e-9 | 3 |
| Netflix 12-bit, 3 frames | 0, 4.4e-9 | 3 |
| Netflix 16-bit, 3 frames | the run failed | 3 |
| Netflix 4:2:2 10-bit, 3 frames | 0, 3.2e-9 | 3 |
| Checkerboard 1 px, 3 frames | 0, 1.0e-7 | 3 |
| Checkerboard 10 px, 3 frames | 0, 1.1e-7 | 3 |
| BBB 3840x2160, 200 frames | 0, 3.1e-7 | 200 |

With `enable_db` and with `enable_db:clip_db` the twin equals the CPU
extractor of the same (icx) build on every frame. Against the GCC build 5 of
48 Netflix frames, 1 of 3 on the 1 px checkerboard, 1 of 3 at 16 bits and 3
of 200 BBB frames differ, by at most 3.6e-15: the linear scores are equal and
the host's `log10` is Intel's in one build and glibc's in the other. Before,
the dB values differed by up to 2.4e-7 (Netflix) and 2.0e-4 (BBB, first 20
frames).

## Open

- The read-back and the host sum (8.6 ms) are the order of the sum, not the
  term. `core/src/feature/ordered_sum.h` adds non-negative terms in a given
  order from parallel pieces; SSIM terms can be negative, so its chunks with
  a negative term fall back to the terms themselves.
- The moment planes are int64 at every bit depth. At 8 to 12 bits every
  horizontal moment fits 32 bits (`256 * 4095^2` is below 2^32).

Both are in `T-SYCL-SSIM-EXACT-THROUGHPUT-2026-10-02`.
