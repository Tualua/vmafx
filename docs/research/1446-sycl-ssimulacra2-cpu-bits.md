<!-- markdownlint-disable MD013 MD060 -->
# Research-1446: SSIMULACRA 2 on SYCL without fp64 — the CPU's fp64 terms in integers, the CPU's sequential sums from parallel pieces, and what a slow single lane does to the design

- **Status**: Active
- **Workstream**: [ADR-1446](../adr/1446-sycl-ssimulacra2-cpu-bits.md), [ADR-1433](../adr/1433-cuda-ssimulacra2-cpu-sum-order.md), [ADR-1443](../adr/1443-sycl-ssim-cpu-arithmetic.md), [ADR-0220](../adr/0220-sycl-fp64-fallback.md), [ADR-1395](../adr/1395-sycl-kernels-no-scratch.md)
- **Last updated**: 2026-10-02

## Question

[Research-1433](1433-cuda-ssimulacra2-cpu-sum-order.md) made the CUDA twin of
`ssimulacra2` return the CPU's score bit for bit: the device evaluates the
CPU's fp64 terms and `core/src/feature/ordered_sum.h` gives the bits of the
CPU's sequential sums. The SYCL twin has no fp64 type. Which part of its
difference from the CPU is the term and which the order, can both be removed
on such a device, and what does it cost on an Arc A380?

## Sources

- CPU: `core/src/feature/ssimulacra2.c` (`ssim_map()`, `edge_diff_map()`,
  `quartic()`), `core/src/feature/ssimulacra2_score.h`
  (`vmaf_ss2_split_edge_difference()`).
- SYCL: `core/src/feature/sycl/ssimulacra2_sycl.cpp` at master `6787b2de1`
  (before) and on `fix/sycl-ssimulacra2-cpu-bits` (after),
  `core/src/feature/sycl/sycl_ssimulacra2_math.h` (new),
  `core/src/feature/sycl/sycl_ordered_sum.h` (new),
  `core/src/feature/sycl/sycl_soft_signed.h`, `core/src/feature/ordered_sum.h`.
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

### 1. The two differences, one at a time

Each row puts one property of the old twin back into the new one. Largest
absolute difference of `ssimulacra2` against `--backend cpu`.

| Property put back | Netflix 576x324 | Checkerboard 1 px | Checkerboard 10 px | BBB 3840x2160 |
|---|---:|---:|---:|---:|
| none (the new twin) | 0 | 0 | 0 | 0 |
| each term the old fp32 pair (its value as a double), added in the CPU's order | 1.1e-12 | 2.0e-13 | 2.7e-12 | 1.0e-12 (20 frames) |
| the exact terms, added per 512-pixel chunk and the chunk sums then in order | 1.3e-13 | 3.4e-13 | 7.2e-11 | 4.5e-13 (2 frames) |
| the old twin (pairs, in a tree of pairs) | 1.1e-12 | 2.6e-13 | 7.6e-11 | 7.2e-13 (200 frames) |

On video the pair is the larger part; on the 10 px checkerboard the order is,
by a factor of 26. The third row equals what the CUDA twin showed before
ADR-1433, with the CPU's terms and its tree (1.3e-13, 3.4e-13 and 7.3e-11).
Neither property alone leaves a twin that matches: with the order put back 6
of 48 Netflix frames are identical and none of the others; with the pair put
back 1 frame of 74 is.

A pair is within about 2^-44 of the term. The sum of doubles needs the term's
53 bits: `ordered_sum.h` works on the bits of each term.

### 2. The terms in integers

Per sample the reference computes, from fp32 values:

- `1.0 - (double)num_m * (double)num_s / (double)denom_s`, clamped at zero.
  The product of two converted floats has at most 48 significant bits and is
  exact in fp64, so the quotient is the first rounding and the difference
  from one the second;
- `(1.0 + ed2) / (1.0 + ed1) - 1.0` with `ed = fabs((double)a - (double)b)`.
  The difference of two converted floats is exact unless their exponents are
  more than 29 apart, and is rounded like any fp64 difference when they are;
- `quartic(x)`: two squarings, each rounded.

`sycl_soft_signed.h` (ADR-1443) has these operations on a sign, a 53-bit
significand and an exponent, each rounded to nearest, ties to even. Two
additions were needed: the exact conversion from an fp32 value (subnormals
included) and the absolute value. `sycl_ssimulacra2_math.h` strings them
together in the reference's order. On the host, 30 million random samples of
the planes' range and 600 000 samples of the cases below, and the same
600 000 in a kernel on the A380, return the reference's bits for all six
terms: identical windows (terms exactly zero), a ratio one fp32 step from
one, a negative `d` (clamped), a negative covariance (`d` above one), edge
operands that cancel, inputs across 60 binades, a zero denominator of either
sign, and the largest finite inputs.

The soft arithmetic covers normal values and zero. Finite fp32 inputs keep
every intermediate value normal; a fourth power overflows fp64 for a term of
2^250 or more, which the header returns as a NaN, and the reference as an
infinity. Either makes the sum not finite and the frame guard rejects the
frame. Finite pictures give terms far below that.

### 3. The sum without fp64

`ordered_sum.h` turns the loop `s += x[i]` into integer increments: while
the sum stays in one binade, adding a term moves it by a whole number of
units in the last place, with a correction of one unit that depends on
whether the sum's last bit is even or odd. Increments compose in order
(`vmaf_ordsum_then()`), so a chunk's increment can be formed by a tree of
adjacent lanes. A walk over the chunks checks each chunk's plan at the exact
sum and adds the chunk's terms one by one where the sum leaves its binade.

Three places used fp64: the plan's prefix sum, the walk's term-by-term adds,
and the function signatures. The functions now have `_bits` forms on the
bit pattern, with the fp64 forms as wrappers, and `VMAF_ORDSUM_NO_FP64`
removes every `double` from the header. On the device:

- the plan takes fp32 advice sums. A plan decides only which chunks are added
  from increments and under which binade; the walk verifies it at the exact
  sum, so a wrong plan costs time and cannot change the result.
  `test_sycl_ordered_sum` gives the walk advice that is half, double, a
  thousandth and a thousand times the true sums, all-zero advice, a constant
  and a NaN, on six kinds of terms and six lengths, and requires the loop's
  bits each time. The advice terms are the old twin's pairs (their high
  words); a fourth power is scaled by 2^88, since `d^4` lies between 2^-212
  and about 2^48 and the fp32 range does not hold it;
- the walk's own adds are `add_bits()`: the term's integer increment while
  the sum stays in its binade, and `signed_add()` otherwise.

### 4. One lane is slow on the A380

The first version walked every chunk without a valid plan term by term,
computing each term in the walk: 410.5 ms per 3840x2160 frame and 78.5 ms per
576x324 frame, against 84.1 and 5.31 ms for the old twin. A step of the walk
(an fp64 addition in integers, or a term) takes about 0.5 to 1 microsecond on
one lane, where the same work spread over a work-group is cheap. A sum
crosses a binade a few dozen times per scale; at the start of a sum, where it
grows from zero, every chunk crosses several.

What reduced it, each version bit-identical on every fixture:

| Version | 3840x2160 | 576x324 |
|---|---:|---:|
| the old twin | 84.1 | 5.31 |
| the walk computes and adds the terms of unplanned chunks | 410.5 | 78.5 |
| a parallel kernel keeps those chunks' terms (128 slots per sum), the walk adds them | 265 | 19.1 |
| and their increments composed into runs of 16 under the expected binade and the next; the plan's advice from fp32 pairs | 231 | 13.8 |
| the unit kernels at SIMD-16 instead of SIMD-8 | 221.4 | 13.4 |
| chunks of 512 instead of 256 | 196.8 | 14.3 |
| runs under the two binades the chunk ends in | 195.5 | 13.6 |
| the final build, 7 runs each | 194.7 | 13.5 |
| chunks of 1024 (slot kernel with the 256-entry register file) | 188.3 | 16.7 |

A kept chunk that crosses one binade is added as runs up to the crossing,
the 16 terms of the run that crosses, and runs after it. A chunk that crosses
several binades spends most of its terms in the last two, because the sum
doubles from one binade to the next; composing the runs under those two
instead of the first two is the last row but two.

### 5. Where the time goes

From builds with stages removed, 3840x2160 and 576x324 (the full build read
191.5 and 13.58 ms in this series):

| Stage | ms at 3840x2160 | ms at 576x324 |
|---|---:|---:|
| upload, conversion, XYB, products, blurs, downsample | 69.1 | 4.81 |
| fp32 advice sums per chunk | 12.8 | 0.33 |
| the plan (one work-item per sum over its chunks) | 7.9 | 0.23 |
| the exact terms as increments, and the kept chunks | 81.7 | 2.74 |
| the walk | 20.0 | 5.47 |

The old combine stage took 14.9 and 0.52 ms. The unit stage is the integer
arithmetic of the terms: two quotients, seven products and six sums or
differences per sample and channel, on 33 million samples per 3840x2160
frame over the scales. The walk is what small frames pay: it does not shrink
with the frame as the parallel stages do, because the first chunks of every
sum cross several binades whatever the size.

### 6. Kernel shapes

All 125 kernels of the build use no scratch memory
(`test_sycl_kernel_scratch`).

| Kernel | Sub-group size | Register file | Otherwise |
|---|---|---|---|
| unit kernels | 16 | default | SIMD-32 spills |
| slot kernel | 16 | 256 | with chunks of 1024 and the default file it spills 1152 bytes |
| walk | 8 | default | |

A spilling variant returned wrong values and took 1075 ms per frame or did
not finish within 300 s, the behaviour ADR-1395 describes for xe.

### 7. Results

`ssimulacra2` against `--backend cpu` of a GCC build, identical frames and
the largest difference:

| Fixture | Before | After |
|---|---|---|
| Netflix 576x324 8-bit, 48 frames | 0, 1.1e-12 | 48 |
| Netflix 10-bit, 3 frames | 0, 7.5e-13 | 3 |
| Netflix 12-bit, 3 frames | 0, 7.3e-13 | 3 |
| Netflix 16-bit, 3 frames | 0, 8.5e-13 | 3 |
| Netflix 4:2:2 10-bit, 3 frames | 0, 1.0e-12 | 3 |
| Checkerboard 1 px, 3 frames | 0, 2.6e-13 | 3 |
| Checkerboard 10 px, 3 frames | 0, 7.6e-11 | 3 |
| BBB 3840x2160, 200 frames | 0, 7.2e-13 | 200 |

Also identical: `yuv_matrix` 1, 2 and 3 on the Netflix pair and the 10 px
checkerboard (153 frames), every fixture against the CPU extractor of the
icx build, and frames that differ from their reference in a handful of
samples by one level. The CUDA twin, which shares `ordered_sum.h`, stays at 0
on the Netflix pair and the 10 px checkerboard with the changed header.

## Open

`T-SYCL-SSIMULACRA2-EXACT-THROUGHPUT-2026-10-02`, none built:

- decide a term's increment from its fp32 pair where the pair is far from a
  rounding boundary of the increment, and evaluate the integers only next to
  one, as `float_adm_sycl` does for its three fp64 expressions (ADR-1434);
- a two-level walk (increments composed over runs of chunks that share a
  binade) and the plan's prefix as a scan across lanes;
- per-run expected binades for the first chunks of a sum.
