<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1433: `ssimulacra2_cuda` returns the sums of the CPU's loops, formed on the device from integer increments per binade

- **Status**: Accepted
- **Date**: 2026-10-01
- **Deciders**: lusoris
- **Tags**: `cuda`, `gpu-parity`, `numerics`, `ssimulacra2`, `testing`, `ci`, `rc3`, `fork-local`

## Context

Since [ADR-1391](1391-cuda-ssimulacra2-device-resident.md) `ssimulacra2_cuda`
reproduces the CPU extractor's planes bit for bit and evaluates the CPU's
fp64 expressions for the per-pixel SSIM and edge terms. One thing differed:
`ssimulacra2.c::ssim_map()` and `::edge_diff_map()` add each of a channel's
six terms pixel after pixel into one `double`, and the twin added them in a
fixed tree. Every add rounds, and where it rounds depends on the running sum,
so another order ends a few units in the last place elsewhere. On an RTX 4090
at `--precision max`, 8 of 113 measured frames equalled `--backend cpu` and
the rest were up to 7.3e-11 away (1.3e-13 on the Netflix 576x324 pair,
1.5e-12 at 3840x2160, 7.3e-11 on the 10 px checkerboard). The gate allowed
`5e-3`.

The maintainer's direction for this lane is that a twin reproduces the CPU
bit for bit and that tuning comes afterwards.

The two twins fixed the same way before
([ADR-1424](1424-cuda-ssim-cpu-frame-sum.md) for `ssim`,
[ADR-1426](1426-cuda-ciede-cpu-arithmetic.md) for `ciede`) read the terms
back and let the host add them. That does not carry over. ssimulacra2 has 18
sums per scale and six scales: the terms of one 3840x2160 frame are 600 MB at
scale 0 alone, against the 864 bytes the twin reads back today, and the twin
has no host stage between scales to add them in.

## Decision

The device forms the CPU's sums itself, with the bits of the CPU's loops.
`core/src/feature/ordered_sum.h` holds the arithmetic, shared by the kernels
and a host test:

- While a running sum of non-negative terms stays in one binade
  `[2^e, 2^(e+1))` it is a multiple of `u = 2^(e-52)`, and adding a term
  moves it by the term rounded to a multiple of `u`. Inside a binade the loop
  is therefore a sum of integers, which may be formed in any grouping.
- The rounding is to nearest, and a tie goes to the even multiple of the
  result, which depends on the parity of the running integer. A run of terms
  carries two increments, one for an even and one for an odd start, and two
  runs compose associatively.
- The plane is cut into chunks of 1024 pixels in raster order. A plan names
  the binade each chunk starts in, from the prefix of the chunks' tree sums.
  A walk over the chunks adds each chunk's increment to the exact sum after
  checking, at the exact sum, that the chunk starts in the planned binade
  and does not leave it. Where the check fails the chunk's 1024 terms are
  added one by one, in pixel order.

The plan is advice: a wrong plan sends chunks to the term-by-term path and
never changes the result. A sum crosses a binade a few dozen times at most,
so few chunks take that path (9 to 27 of 8100 per sum on a 3840x2160 frame).

Four kernels per scale replace the two of the tree
(`ssimulacra2_chunk_sums`, `ssimulacra2_chunk_plan`,
`ssimulacra2_chunk_units`, `ssimulacra2_ordered_totals`). The readback stays
the 864-byte block of sums, and `collect()` is unchanged.

`ssimulacra2` on `cuda` joins `EXACT_TWINS`: the gate compares the cell at
tolerance 0.

## Alternatives considered

| Option | Result | Verdict |
|---|---|---|
| Read the terms back and add them on the host (the `ssim` and `ciede` way) | 600 MB per 3840x2160 frame at scale 0, about 800 MB over the pyramid; undoes the one-readback design of ADR-1391 | Rejected |
| One device thread per sum adds every term | Measured by sending every chunk to the term-by-term path: the same scores at 320 ms per 4K frame | Rejected: 2.5 times the 16-thread CPU extractor's 126 ms |
| A correctly rounded sum (error-free accumulation, one rounding at the end) | Not the CPU's result: the CPU's loop rounds 8.3 million times. Would need the CPU extractor changed to match | Rejected: changes the reference |
| Keep the tree and tighten the gate to the measured 7.3e-11 | Not exact; the row stays open | Rejected: exactness is reachable |
| Plan from an fp32 evaluation of the terms (cheaper first pass) | `d = 1 - q` with `q` near 1 loses all its digits in fp32, so the plan is wrong over flat, nearly identical regions and those chunks all take the term-by-term path | Not now: a candidate for the tuning pass with a repair pass behind it |
| Send a chunk with a tie to the term-by-term path instead of carrying both parities | The terms `1 - q` are multiples of 2^-53, so every second one ties while the sum is in `[1, 2)`; on low-distortion content the sum can stay there for a large share of the plane | Rejected: a throughput cliff on ordinary content |
| **Integer increments per binade, both parities, checked walk, term-by-term fallback** | Bit-identical on every fixture; 15.6 ms per 4K frame instead of 7.8 | **Chosen** |

## Consequences

- **Positive**: `ssimulacra2_cuda` equals `--backend cpu` on all 113 measured
  frames (Netflix 576x324 at 8, 10, 12 and 16 bits, both 1080p checkerboard
  pairs, BBB 3840x2160) and on 200 BBB frames through the gate, with every
  `yuv_matrix`. The cell is compared at 0 instead of `5e-3`.
  `ordered_sum.h` is not specific to ssimulacra2: it serves any sum of
  non-negative doubles a CPU extractor forms in one loop.
- **Negative**: the twin is slower. A run of the twin alone takes 15.6 ms per
  3840x2160 frame instead of 7.8 ms and 1.7 ms instead of 0.4 ms at 576x324
  (the CPU extractor: 510 ms on one thread, 126 ms on sixteen at 4K). The
  terms are evaluated twice (once for the plan's tree sums, once for the
  increments), and two kernels walk the chunks on one lane.
  `T-CUDA-SSIMULACRA2-EXACT-THROUGHPUT-2026-10-01` carries the attribution
  and the candidates. The twin holds 3.8 MB more device memory at 3840x2160
  and launches 48 kernels per frame instead of 36.
- **Neutral / follow-ups**:
  - The arithmetic needs terms that are non-negative or NaN. ssimulacra2's
    are: `d` is clamped at zero, the edge terms are split into two
    non-negative halves, and a non-finite term is kept as such
    (`vmaf_ss2_split_edge_difference()`). A change that lets a term go
    negative must not reach `ordered_sum.h`.
  - Stored `ssimulacra2_cuda` scores change by up to 7.3e-11.
  - `ssimulacra2_sycl` and `ssimulacra2_hip` add fp32 pairs in a fixed tree
    and keep the `5e-3` cell: `T-GPU-SSIMULACRA2-SUM-ORDER-2026-10-01`.
  - `ciede_cuda` adds non-negative float terms into one double on the host
    after a 33 MB readback; the same header would form that sum on the
    device (`T-CUDA-CIEDE-EXACT-THROUGHPUT-2026-10-01`). `integer_ssim_cuda`'s
    terms can be negative and need the signed form sketched in
    [Research-1424](../research/1424-cuda-ssim-cpu-frame-sum.md).

## References

- `req` (maintainer brief, 2026-10-01): "results before speed; a twin
  reproduces the CPU bit for bit, and tuning comes afterwards."
- [ADR-1391](1391-cuda-ssimulacra2-device-resident.md),
  [ADR-1424](1424-cuda-ssim-cpu-frame-sum.md),
  [ADR-1426](1426-cuda-ciede-cpu-arithmetic.md),
  [ADR-1403](1403-cuda-strict-fp-every-kernel.md),
  [ADR-1397](1397-psnr-hvs-twins-cpu-float-sum.md),
  [ADR-0214](0214-gpu-parity-ci-gate.md).
- [Research-1433](../research/1433-cuda-ssimulacra2-cpu-sum-order.md),
  [Research-1424](../research/1424-cuda-ssim-cpu-frame-sum.md) (section 3,
  the per-binade integer sum as a candidate).
