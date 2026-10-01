<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1432: `vif_sycl` computes the gain terms of the integer VIF in exact integer arithmetic and returns the CPU's scores bit for bit

- **Status**: Accepted
- **Date**: 2026-10-01
- **Deciders**: lusoris
- **Tags**: `sycl`, `gpu-parity`, `numerics`, `vif`, `testing`, `ci`, `rc3`, `fork-local`

## Context

`integer_vif.c` is integer arithmetic except in one place. For a pixel of the
log branch it forms the gain in fp64 and truncates two results to integers
before the log2 table (`vif_accumulate_pixel()`, and the same operations in
`x86/vif_avx2.c`, `x86/vif_avx512.c` and `arm64/vif_neon.c`):

```c
const double eps = 65536 * 1.0e-10;
double g = sigma12 / (sigma1_sq + eps);
int32_t sv_sq = sigma2_sq - g * sigma12;
sv_sq = (uint32_t)(MAX(sv_sq, 0));
g = MIN(g, vif_enhn_gain_limit);
... (int64_t)((g * g * sigma1_sq)) ...
```

A SYCL kernel has no fp64 type ([ADR-0220](0220-sycl-fp64-fallback.md)), so
`vif_sycl` divided in fp32, formed `sv_sq` with one `sycl::fma` and the
product in fp32. For a share of the pixels one of the two integers came out
one off, the table lookup differed, and a scale's numerator sum ended one or
a few fp32 steps from the CPU's. With the host tail fixed (#1744) that was
all that separated the twin from the CPU, measured on an Arc A380 at
`--precision max`: scores identical on 41 / 31 / 16 / 12 of the 48 Netflix
576x324 frames for scales 0 to 3, on 196 / 174 / 184 / 140 of 200 BBB
3840x2160 frames, largest difference 3.6e-7
(`T-SYCL-VIF-FP32-GAIN-2026-10-01`). `vif_cuda` has fp64 on the device and
equals the CPU.

`vif` is a feature of every shipped VMAF model, and the direction for the GPU
twins is that a twin returns the CPU's bits; speed comes afterwards.

## Decision

We will compute the two integers exactly in the kernel, in integer
arithmetic, and replay the reference's fp64 operations in 64-bit integers for
the samples that integer arithmetic does not decide.

**One integer division decides both.** In exact arithmetic the two values are

```text
sigma2_sq - sigma12^2 / (sigma1_sq + eps)
sigma12^2 * sigma1_sq / (sigma1_sq + eps)^2
```

With `q` and `r` the quotient and remainder of `sigma12^2 / sigma1_sq`, the
first is `sigma2_sq - q - r / sigma1_sq` plus a term in `eps`, the second
`q + r / sigma1_sq` minus twice that term. `gain_terms_integer()`
(`core/src/feature/sycl/sycl_integer_vif_math.h`) forms `q` and `r` exactly
and the `eps` term in fp32, in units of 2^-8 of `1 / sigma1_sq`, and reads
both integer parts off them. `eps` enters as the reference has it: `fl64(sigma1_sq +
eps)` is `sigma1_sq` with `eps` rounded to the last place of that binade
(`divisor_of()`), which for a large variance is 2% away from `eps` itself.

**Zones.** The fp64 chain differs from the exact value by its own rounding:
at most 2^-20 for `sv_sq` (three roundings of values below 2^31), at most
2^-51 of the value for the product (four roundings). Where the exact value is
closer than that, plus the error of the fp32 `eps` term, to an integer at
which the result changes, the integer evaluation reports the sample as
undecided. Around 0 the truncation gives 0 from both sides, so identical
planes (`sigma12 = sigma1_sq = sigma2_sq`, a file scored against itself) are
decided. Counted on the device: one pixel in 233 000 is undecided on the
Netflix 576x324 pair and one in 313 000 on BBB 3840x2160, about 35 per 4K
frame over the four scales.

**Replay.** An undecided sample gets `gain_terms_replayed()`: the reference's
six operations on `SoftDouble` values (a 53-bit significand and an exponent),
each rounded to nearest even as the fp64 operation it stands for. The result
is the reference's by construction. The operations live in
`core/src/feature/sycl/sycl_soft_double.h`, shared with the `float_vif` twin
([ADR-1422](1422-sycl-float-vif-cpu-arithmetic.md)), which had its own copy
of three of them.

**The gain limit.** `g < vif_enhn_gain_limit` is `sigma12 <= limit *
sigma1_sq` when the limit is an integer: the two sides of the fp64 comparison
then differ by at least `eps / sigma1_sq`, far more than a rounding. At an
integer limit the product `limit^2 * sigma1_sq` is exact in fp64. Any other
limit is compared in fp32 with a margin, and a pixel at such a limit, or
inside the margin, is replayed. The host converts the option once
(`make_gain_limit()`).

**No 64-bit divider, no `sycl::mul_hi()`.** The quotient comes from two fp32
estimates and an integer correction (`divide()`). The 106-bit products of the
replay are formed in 32-bit limbs (`u128_mul()`): `sycl::mul_hi()` on 64-bit
operands returned wrong values in a kernel on the Arc A380.

**No scratch memory.** The statistic carries a pixel's seven accumulator
terms as `int32_t` (`vif_terms`) and widens them when it adds; as `int64_t`
they held 28 of a SIMD-16 kernel's 128 registers. The fused kernel of scale 0
takes the 256-entry register file at SIMD-16 as well
(`vif_fused_grf_size()`); with the default file it spilled 128 bytes
([ADR-1395](1395-sycl-kernels-no-scratch.md)).

**Gate.** `vif` is declared exact for `sycl` by the fragment file
`scripts/ci/exact_twins.d/vif.sycl`
([ADR-1428](1428-exact-twins-fragments.md)): the CPU ↔ SYCL cell is compared
with tolerance 0 at `--precision max`.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Integer quotient with zones, replay for the undecided (this ADR) | The CPU's integers on every pixel by construction; fp64-free; scratch-free; 0.7 ms per 3840x2160 frame | The most code: an integer evaluation, its error bounds and a soft-fp64 replay | Chosen |
| Exact fp32 pairs for the gain, replay next to an integer | The technique of ADR-1422 | A pair is good to 2^-44 of a value near 2^31, so every pixel within about 2^-12 of an integer would be replayed, one in a few hundred by that bound, against one in 300 000 measured here | The replay is the expensive part |
| The replay on every pixel | One path, no error analysis | A 56-step division and three 106-bit products per pixel of every scale | Not measured; the integer path costs 0.7 ms |
| Flag undecided pixels and finish them on the host in real fp64 | No soft fp64 on the device | A data-dependent readback and a buffer that can overflow (a non-integer limit with identical planes flags every pixel) | The in-kernel replay is self-contained |
| Keep the fp32 gain and the 5e-5 tolerance | No change | The twin is 3.6e-7 from the CPU on a feature of every shipped model | The direction is bit for bit |
| Make the CPU compute the gain in integers | Twins and SIMD paths could share it | Changes scores the Netflix golden assertions pin | [ADR-0024](0024-netflix-golden-preserved.md) |

## Consequences

- **Positive**: measured on an Arc A380 (xe, Level Zero, icpx 2026.0) at
  `--precision max`, every output of every frame equals `--backend cpu`: the
  Netflix 576x324 pair at 8 bits (48 frames), at 10, 12 and 16 bits and as
  4:2:2 10-bit (3 frames each), both 1920x1080 checkerboard pairs (3 frames
  each) and BBB 3840x2160 (200 frames), with `debug=true` (15 outputs). Also
  identical: `vif_enhn_gain_limit` of 1.0, 1.2 and 37.5, `vif_skip_scale0`,
  and a 3840x2160 clip scored against itself. The same holds against a GCC
  build of the CPU extractor and against the CPU extractor of the icx build.
- **Positive**: `sycl_integer_vif_math.h` was compared on the host with the
  reference's fp64 lines on 2.4e9 samples, random and on every boundary it
  distinguishes: no decided sample wrong, no replayed sample wrong.
- **Negative**: 0.75 ms more per 3840x2160 frame through the `vmaf` tool on
  the A380: 21.46 ms before, 22.21 after (medians of 11 paired 100-frame
  runs, host load 6 to 16); 576x324: 0.79 and 0.88 ms.
- **Negative**: a pixel at a non-integer `vif_enhn_gain_limit` is replayed,
  and so is every pixel when such a limit meets identical planes. The option
  defaults to 100 and the shipped models set 1.
- **Neutral / follow-ups**:
  - The header mirrors six lines of `integer_vif.c`. If they change upstream,
    the header changes in the same PR; `test_sycl_vif_exact_gain_contract.py`
    fails when the lines move, and `test_sycl_integer_vif_math` compares the
    header with them on the host and in a kernel on the device.
  - `test_sycl_vif_parity` asserts equality on every output; it failed on the
    fp32 gain.
  - No kernel of the twin uses scratch memory (`test_sycl_kernel_scratch`,
    117 kernels audited on the A380).
  - `integer_vif_metal` makes the same fp32 trade-off and can take the same
    header's arithmetic; not measured here.
  - `sycl::mul_hi()` on 64-bit operands is not used by any SYCL twin; the
    contract test rejects it in these files.

## References

- `req` (maintainer brief for the SYCL exactness lane, 2026-10-01): "results before speed; a twin reproduces the CPU bit for bit, tuning comes afterwards".
- [ADR-1422](1422-sycl-float-vif-cpu-arithmetic.md) (the replay technique and
  its primitives), [ADR-1397](1397-psnr-hvs-twins-cpu-float-sum.md) (the
  exact cell), [ADR-1395](1395-sycl-kernels-no-scratch.md),
  [ADR-0220](0220-sycl-fp64-fallback.md),
  [ADR-1367](1367-sycl-strict-fp-every-feature-tu.md),
  [ADR-0214](0214-gpu-parity-ci-gate.md),
  [ADR-0024](0024-netflix-golden-preserved.md).
- `docs/state.md`: `T-SYCL-VIF-FP32-GAIN-2026-10-01` (closed by this
  decision).
