<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1499: The `float_psnr` twins add each row's exact sum in the CPU's order, and return the CPU's bits on every frame

- **Status**: Accepted
- **Date**: 2026-10-03
- **Deciders**: lusoris
- **Tags**: `cuda`, `sycl`, `hip`, `gpu-parity`, `numerics`, `psnr`, `testing`, `rc3`, `fork-local`
- **Supersedes**: the bound past 2^53 units stated in [ADR-1440](1440-hip-float-psnr-exact-block-sums.md), [ADR-1450](1450-sycl-float-psnr-exact-block-sums.md) and [ADR-1455](1455-cuda-float-psnr-exact-block-sums.md). Their integer block sums stand.

## Context

`float_psnr.c::extract()` forms each sample difference's square in `float`,
adds a row's squares into a `double` (`noise_line()`, scalar or AVX2,
AVX-512, NEON) and adds the rows into one `double`. The CUDA, SYCL and HIP
twins add the same float squares as integers in units of 1 / scaler^2, per
16x16 block, and divided the exact frame total on the host. That is the
CPU's sum while it is at most 2^53 units. On a 16-bit frame whose mean
squared error times its pixel count passes 2^37 on the 8-bit scale the
CPU's adds of the rows round, and the twins, which rounded the exact total
once, were within a stated bound: 0 of 8 frames of 16-bit 3840x2160 noise
(reference in the upper half of the range, distorted frame in the lower)
identical on the RTX 4090 and the gfx1036, 1 of 8 on the Arc A380. No ledger
row recorded the gap. After the same decision for `float_moment`
([ADR-1497](1497-float-moment-twins-cpu-sum-past-2-53.md)), the maintainer
decided that `float_psnr` must return the CPU's bits on every input for
rc.3 as well.

## Decision

We will lay each block (CUDA, HIP) or work-group (SYCL) out over 256 pixels
of one row instead of 16x16 pixels, and form the CPU's sum on the host with
one helper for the three twins, `core/src/feature/float_psnr_rows.h`
(`vmaf_float_psnr_row_noise()`): each row's segment sums added in 64-bit
integers, the rows added into one `double` in order, then the CPU's two
divisions. The HIP kernel puts its two 16-bit halves together into one
`uint64` per block.

Why it is the CPU's double on every input:

1. In units of 1 / scaler^2 every term is an integer below 2^32, the float
   square of the sample difference (the twins' term since ADR-1440 / 1450 /
   1455). A row has at most 2^15 terms, so every partial sum inside a row is
   an integer below 2^47, exact in a `double` in any order: every
   `noise_line()` variant returns the row's exact integer sum.
2. The device's segment sums and the host's 64-bit row sums are exact
   integers of the same terms, so the host holds each row's sum exactly,
   and converting it to `double` is exact (below 2^47).
3. The host adds those values into a `double` row after row, the CPU's adds
   of the same operands in the same order: every rounding past 2^53, ties
   included, is the CPU's.
4. The CPU adds the rows' true values (the integer sums times 2^-2k for
   scaler = 2^k); multiplying every operand by a power of two scales every
   exactly-rounded result by it while nothing underflows or overflows (the
   smallest nonzero value is 2^-16), so the host's sum in units, divided by
   scaler^2, is the CPU's sum. The division by the pixel count is the CPU's.

No device arithmetic of the ordered-sum kind is needed: the exact-sum
machinery of `float_moment_sum.h` exists because `moment.c` adds pixel by
pixel; `float_psnr.c` adds row by row, and its rows are exact.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Blocks of one row, the rows' exact sums added in order on the host (this ADR) | The CPU's adds of the CPU's operands, by the argument above; same number of blocks and read-back bytes as before; no new kernel | The block shape changes (256 x 1) | Chosen |
| Keep 16x16 blocks and read back per-row sums with 64-bit atomics | Fewer host adds | An atomic per row segment, a buffer to clear per frame, and a second layout | Not needed: a block of one row already is a segment |
| `float_moment_sum.h`'s walk on the device | One exact-sum design for both extractors | Four kernels and a walk to reproduce a sum the host forms with `h` adds | The CPU's rows are exact; nothing to emulate |
| Keep the bound | No change | Not the CPU's bits | The maintainer decided against it (References) |

## Consequences

- **Positive**: at `--precision max` against `--backend cpu`, every frame
  identical on an RTX 4090, an Arc A380 and a gfx1036: 8 of 8 frames of the
  16-bit 3840x2160 half-range noise above (0, 1 and 0 of 8 before), 16 of 16
  of 16-bit 3840x2160 full-range noise, 32 of 32 of BBB 3840x2160 widened to
  16 bits in each of three ways. The parity gate's `float_psnr` cell reads 0
  at tolerance 0 on the Netflix pair and on the half-range noise on each
  device (FAIL on master there). `test_{cuda,sycl,hip}_float_psnr_parity`
  assert `==` on that frame and on frames whose exact sum is 2^53 - 1, 2^53,
  and 2^53 followed by three rows of sum 1; each fails on master.
  `test_float_psnr_rows` holds the helper against the CPU extractor on the
  host, a 7680x4320 frame included.
- **Positive**: no measurable cost (per 16-bit 3840x2160 frame, before and
  after: 4.12 and 4.12 ms on the RTX 4090, 7.16 and 7.06 ms on the A380,
  10.9 and 9.3 ms on the gfx1036).
- **Neutral / follow-ups**:
  - The HIP parity test now uses `core/test/float_psnr_twin_parity.h`, as the
    CUDA and SYCL tests do (the RC5 follow-up of ADR-1455).
  - `float_psnr_metal` still adds fp32 block sums
    (`T-METAL-FLOAT-PSNR-FP32-BLOCK-SUMS-2026-10-02`); its fix should take
    this layout and helper too.
  - Guards: `test_float_psnr_rows` (host), the three
    `test_*_float_psnr_exact_contract.py` (a block that spans rows, a frame
    total instead of rows, planted), the three device parity tests.

## References

- `Q` (maintainer popup, 2026-10-03, float_psnr past 2^53 units): "Make it exact for rc.3 (Recommended)".
- [ADR-1497](1497-float-moment-twins-cpu-sum-past-2-53.md): the same decision for `float_moment`.
- `docs/state.md`: `T-GPU-FLOAT-PSNR-PAST-2-53-2026-10-03` (opened and closed by this ADR).
