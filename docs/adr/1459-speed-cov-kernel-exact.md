<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1459: SpEED's covariance kernels return the scalar kernel's bits; they vectorise across sums, not within one

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `numerics`, `simd`, `speed`, `avx2`, `avx512`, `neon`, `upstream-divergence`, `testing`, `rc3`, `fork-local`

## Context

SpEED builds a 25x25 covariance matrix per plane: 325 sums
`sum += (x - mean_x) * (y - mean_y)` over the same block, taken at 25 offsets
(`compute_covariance_matrix()` in `core/src/feature/speed.c`). Upstream
vectorised one sum at a time (`30f472b14`: AVX2 and AVX-512; `15297286`:
NEON): the products of one sum go into 8 or 16 partial sums with fused
multiply-adds, and the partial sums are added at the end. That is not the
scalar kernel's running sum, so the result differs in the last bits.

The fork carried the x86 kernels under a written 1e-9 tolerance
(`core/test/test_speed_simd.c`, `core/src/feature/x86/AGENTS.md`), while both
SIMD directories state bit-exactness against the scalar reference as the rule,
and the NEON kernel was left out for that reason when Netflix/vmaf#1653 was
ported (`T-SPEED-COV-KERNEL-X86-NOT-BIT-EXACT-2026-10-02`). Measured on a
Ryzen 9 9950X3D (GCC 16.2.1, 23 100 sums: 35 widths x 11 heights x 3 layouts
x 2 mean choices x 10 input patterns): the AVX2 kernel differs from
`compute_cov_kernel_scalar()` on 5351 sums and the AVX-512 kernel on 4973, by
up to 6.5e-12 relative. No score differed on the fixtures measured, because
the covariance is rounded to `float` after the sum; that is a property of
those fixtures, not of the kernels.

The maintainer decided that the x86 kernels return the scalar kernel's bits on
every input now, and that speed is tuned afterwards (RC7).

## Decision

The covariance kernels vectorise across sums. A row kernel
(`speed_cov_row_fn`, `core/src/feature/speed_cov.h`) pairs one block `x` with
up to five blocks `y_k` that start at consecutive columns of one row of the
block grid, which is how the 25 offsets are laid out, and returns their five
sums. Lane `k` holds the running sum of `y_k` and adds its own product, in the
scalar kernel's order: one subtraction per operand, one multiplication, one
addition, each rounded on its own. No fused multiply-add, and no lane ever
feeds another, so every sum has the bits of `compute_cov_kernel_scalar()`.
`compute_covariance_matrix()` walks the lower triangle one row of `y` blocks
at a time. AVX2 (`x86/speed_avx2.c`), AVX-512 (`x86/speed_avx512.c`) and NEON
(`arm64/speed_neon.c`) implement the row kernel; upstream's three kernels are
not in the tree.

`compute_cov_kernel_scalar()` is the reference and is pinned: the product is
its own statement and the function carries the function-scoped no-contraction
guard of ADR-1057. Without it clang on aarch64 fused the remainder of the loop
and left its vector body unfused, and no kernel could match it at every width.

## Alternatives considered

Timings: nanoseconds per covariance sum, Ryzen 9 9950X3D, one pinned core,
minimum of 7, GCC 16.2.1 with the libraries' flags.

| Block (plane) | scalar | upstream AVX2 | upstream AVX-512 | ordered AVX2 | ordered AVX-512 | row AVX2 | row AVX-512 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 11x6 (576x324 chroma) | 24.4 | 14.3 | 11.9 | 21.1 | 20.4 | 10.7 | 7.3 |
| 31x16 (576x324 luma) | 211 | 66.8 | 62.4 | 192 | 194 | 79.6 | 55.3 |
| 56x26 (1080p chroma) | 645 | 142 | 84.0 | 589 | 581 | 225 | 165 |
| 116x61 (1080p luma) | 3337 | 738 | 484 | 3140 | 3064 | 1167 | 823 |
| 236x131 (2160p luma) | 14 678 | 3234 | 2101 | 15 138 | 14 668 | 5310 | 3737 |

| Option | Pros | Cons | Why not chosen |
| --- | --- | --- | --- |
| Keep upstream's kernels under a written tolerance, and take the NEON one | Upstream's code and speed on every target | The directory rule gets a standing exception; a sum that rounds differently is one `float` rounding away from a different score; SIMD and scalar dispatch are not interchangeable | The maintainer chose exactness |
| "Ordered" kernel: vectorise the products of one sum, add them into one running sum in index order | Smallest change; exact (0 of 23 100 sums differ) | The additions stay one serial chain, which is what bounds the scalar loop already: within 15 % of scalar at every size. GCC and clang emit this form for the scalar loop by themselves | No gain over scalar |
| Dispatch the scalar kernel on x86 and delete the SIMD kernels | Nothing to maintain | 4.5x slower than upstream's AVX2 kernel and 7x slower than its AVX-512 kernel at 2160p luma | The row kernel is exact and 2.3x to 4.1x faster than scalar |
| `core/src/feature/ordered_sum.h` (ADR-1433) | Reproduces a sequential sum from parallel pieces | Requires non-negative terms; covariance products are signed | Does not apply |
| **Row kernel: one lane per sum (chosen)** | Exact on every input (0 of 346 500 sums differ in the harness, 0 of 15 390 per kernel in the test); 2.3x to 2.9x (AVX2) and 3.3x to 4.1x (AVX-512) faster than scalar; faster than upstream's kernels on the 576x324 chroma block; carries over to NEON unchanged | 1.6x to 2.0x slower than upstream's kernels on the blocks of 1080p and 2160p planes: five of eight AVX-512 lanes are used and each step is one add deep | Chosen |
| Row kernel over all rows of `y` blocks at once (up to five accumulators per pass) | Measured in the harness: 945 us per 2160p luma matrix against 1215 us for the row kernel (AVX-512), 1539 against 1726 (AVX2) | Five specialisations per kernel or masked dead rows; still 1.57x upstream's kernels | Left for the tuning row |

At the extractor (`speed_chroma` + `speed_temporal`, CPU ms per frame,
`(t(40) - t(8)) / 32`, median of 5 interleaved runs on one pinned core, load
average 46 to 60 on 32 threads, golden-profile builds of master `6d2b9ffa6`
and of this change):

| Plane | scalar | AVX2 | AVX-512 |
| --- | --- | --- | --- |
| 576x324 | 9.15 -> 9.30 | 1.55 -> 1.56 | 1.30 -> 1.25 |
| 1920x1080 | 101.3 -> 101.0 | 8.94 -> 9.08 | 8.11 -> 8.68 |
| 3840x2160 | 391.5 -> 403.1 | 35.56 -> 38.24 | 35.02 -> 39.22 |

## Consequences

- **Positive**: on x86 the scalar, AVX2 and AVX-512 dispatch of
  `speed_chroma` and `speed_temporal` compute the same covariance bits, and so
  does NEON on aarch64, under GCC and under clang. `test_speed_simd` compares
  every sum with `memcmp` against the production reference (15 390 sums per
  kernel: block sizes from 1x1 to 256x64, nine input patterns including
  signed zeros, subnormals, cancelling 1e30 terms and overflowing products,
  tight, padded and unaligned planes, counts 1 to 5), and the planes are
  allocated at exactly the readable size so a sanitizer build sees an
  over-read. The 1e-9 tolerance and the `AGENTS.md` exception are gone.
  aarch64 gets a SIMD covariance kernel for the first time.
- **Negative**: `speed_chroma` + `speed_temporal` cost up to 12 % more CPU
  time per frame on x86 (3840x2160, AVX-512; 8 % with AVX2; 7 % at 1920x1080
  with AVX-512; within noise at 576x324). The kernel itself is 1.6x to 2.0x
  slower than upstream's on large blocks. The fork no longer carries
  upstream's covariance kernels, so an upstream change to them has to be read
  and ported by hand.
- **Neutral / follow-ups**:
  - Scores do not change on x86: 60 of 60 reports at `--precision max` are
    byte-identical before and after (scalar, AVX2, AVX-512 dispatch). On
    aarch64 a GCC build is unchanged as well; a clang build changes in the
    last bits of the covariance wherever its scalar loop had fused a
    remainder.
  - `T-SPEED-COV-KERNEL-EXACT-THROUGHPUT-2026-10-02` (RC7) holds the numbers
    above and the two candidates not built: all rows of `y` blocks in one
    pass, and packing the three idle AVX-512 lanes with a second `x` block.
  - NEON speed is not measured: the kernel ran under qemu only. Its inner
    loop is 20 instructions for five sums (4 per sum and element) where the
    scalar loop GCC 16.1 compiles is 13 for two elements of one sum (6.5), and
    five sums advance per add; what that is worth has to be measured on
    hardware.
  - The GPU twins compute the covariance on the device and are not touched.

## References

- `Q` (popup answer of the maintainer on
  `T-SPEED-COV-KERNEL-X86-NOT-BIT-EXACT-2026-10-02`, 2026-10-02):
  "Exact now, speed in RC7".
- [ADR-0138](0138-iqa-convolve-avx2-bitexact-double.md),
  [ADR-0139](0139-ssim-simd-bitexact-double.md) (bit-exact SIMD),
  [ADR-1057](1057-revert-float-adm-simd-dispatch-neon-fma.md) (function-scoped
  no-contraction guard), [ADR-1415](1415-x86-simd-libraries-strict-fp.md),
  [ADR-1433](1433-cuda-ssimulacra2-cpu-sum-order.md) (`ordered_sum.h`).
- Netflix/vmaf `30f472b14` (AVX2 and AVX-512 covariance kernels) and
  `15297286` (NEON covariance kernel, Netflix/vmaf#1653).
- `core/src/feature/speed_matmul.h`: the same "the vector axis is an output
  index" argument for SpEED's matrix product.
