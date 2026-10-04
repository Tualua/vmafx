<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1561: integer VIF converts its residual variance through `vif_sv_sq()`, which returns x86's value without the undefined `double` to `int32_t` conversion

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: lusoris
- **Tags**: `numerics`, `vif`, `simd`, `cuda`, `hip`, `aarch64`, `upstream-parity`, `rc3`, `fork-local`

## Context

`vif_accumulate_pixel()` in `core/src/feature/integer_vif.c` keeps
Netflix's lines for the residual variance of the gain model:

```c
double g = sigma12 / (sigma1_sq + eps);
int32_t sv_sq = sigma2_sq - g * sigma12;
sv_sq = (uint32_t)(MAX(sv_sq, 0));
```

The same lines are in `x86/vif_avx2.c` (`vif_num_log256()`),
`arm64/vif_neon.c` (`vif_num_log()`), the CUDA kernel
(`cuda/integer_vif/vif_statistics.cuh`) and the HIP kernel
(`hip/integer_vif/vif_statistics.hip`). In the log branch `sigma1_sq` is at
least 2^17 and `sigma12` below 2^31, so `g` is at most 2^14 and the `double`
difference lies between about -2^45 and 2^31. Converting a value whose
integral part `int32_t` cannot represent is undefined behaviour (C11, N1570
6.3.1.4 paragraph 1;
`T-INTEGER-VIF-SV-SQ-CONVERSION-UB-2026-10-03`).

What the targets do with it:

| Target | Conversion | Value below INT32_MIN after the clamp |
|---|---|---|
| x86-64, scalar (GCC 16.2, clang 23.1) | `cvttsd2si` (32-bit): INT32_MIN, the "integer indefinite" value | 0 |
| x86-64, AVX-512 vector path (`vif_avx512.c`) | `_mm512_cvttpd_epi64`, defined for the whole range | 0 |
| aarch64, scalar (`fcvtzs w`) | saturates to INT32_MIN | 0 |
| aarch64, a vectorised loop (GCC 16.1 and clang 23.1, `-O2` and `-O3`) | `fcvtzs v.2d` and a narrowing move: the low 32 bits of the 64-bit result | often positive |

A loop over arrays of random variance triples (100,000 triples, 23,094 of
them below INT32_MIN) gives x86's values on aarch64 when the conversion is
in a `noinline` function and 15,100 different values when the loop is
vectorised, with either compiler. The library as built today does not
vectorise the statistic: `test_integer_vif_sv_sq` returns x86's values on
aarch64 GCC and clang builds under qemu-user before this change. The
behaviour is still undefined, and clang's `-fsanitize=undefined` (the
configuration of the sanitizer job in `.github/workflows/sanitizers.yml`)
stops the same test before the change with
`integer_vif.c:318:29: runtime error: -3.97975e+09 is outside the range of
representable values of type 'int'`.

The out-of-range branch needs a moment triple that real pixel windows are not
expected to produce: for consistent moments `g * sigma12 = sigma12^2 /
sigma1_sq` is at most `sigma2_sq` up to the fixed-point rounding
(Cauchy-Schwarz). The random variances of `test_sycl_integer_vif_math` and of
the new test reach it on about a quarter of their samples.

A second undefined operation sits next to it: `sv_sq + sigma_nsq` was an
`int32_t` addition, which overflows for `sv_sq` above 2^31 - 2^17.

The fork's rule is that code inherited from Netflix evaluates as Netflix's
source does and a difference needs an ADR
([ADR-1487](1487-upstream-parity-policy-and-guard.md)).

## Decision

`core/src/feature/integer_vif_sv_sq.h` defines `vif_sv_sq(sigma2_sq, g,
sigma12)`: it forms the same `double` difference and returns it truncated
toward zero when it lies in (0, 2^31), 0 otherwise. That is the value x86
computes from Netflix's lines for every input, so x86 scores keep their bits,
and the result is defined on every target. The scalar statistic, the AVX2 and
NEON per-pixel helpers, the CUDA and HIP kernels and the reference of
`test_sycl_integer_vif_math.c` call it; kernels set `VMAF_IVIF_FUNC` to their
device qualifiers. `sv_sq` is a `uint32_t`, so `sv_sq + sigma_nsq` is an
unsigned addition with the bits the `int32_t` addition had on x86. The
AVX-512 vector path and the SYCL kernel, which already form the value without
an undefined operation ([ADR-1432](1432-sycl-integer-vif-exact-gain.md)), are
unchanged.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| `vif_sv_sq()`: test the `double`, convert only in range (chosen) | Defined everywhere; x86's bits by construction; one definition the kernels compile too | A deviation from Netflix's source text | Keeps every score while removing the undefined behaviour |
| Convert through `int64_t`, then clamp | Defined (the difference is above -2^46); a one-line change | Gives a different value than x86 for `sigma2_sq - g * sigma12 >= 2^31` (unreachable today, but not x86's value) and differs in form from the SYCL and Metal integer selections | Not x86's value for every input |
| Leave Netflix's lines and rely on the targets | No deviation | Undefined behaviour; a compiler may vectorise the loop and change scores on aarch64; the sanitizer job fails on it | The behaviour must be defined |
| Clamp the `double` with `fmax`/`fmin`, then convert | Defined | Two library calls per pixel in the hot loop; still a different spelling in every twin | No gain over a comparison |

## Consequences

- **Positive**: integer VIF has no undefined conversion on any CPU or GPU
  path that mirrors the statistic, and an aarch64 build that vectorises the
  loop scores as x86 does. `test_integer_vif_sv_sq` and
  `test_integer_vif_sv_sq_contract.py` fail if a mirror converts the raw
  difference to an integer again.
- **Neutral**: no score changes. On x86 the Netflix 576x324 pair and both
  1920x1080 checkerboard pairs give the same 4,860 values (2,430 of them
  `vif`) before and after at `--precision max`, at the scalar, AVX2 and
  AVX-512 dispatch levels. The upstream parity guard needs no fragment: the
  outputs are Netflix's.
- **Neutral / follow-ups**: the Metal kernel on master forms the term in fp32
  and is no mirror of the fp64 expression; the Metal port to the integer
  selection (PR #1921) forms the value without a conversion; its test's
  reference must call `vif_sv_sq()` once this lands (the contract test scans
  every C, C++, CUDA, HIP, Objective-C++ and Metal file under
  `core/src/feature` and `core/test`).

## References

- Source: maintainer follow-up brief of 2026-10-04, lane CORE item 1
  (paraphrased): make the conversion defined, keep x86's bits exactly, change
  every mirror together, and record the deviation from Netflix's source in an
  ADR.
- ISO/IEC 9899:201x committee draft N1570, 6.3.1.4 "Real floating and
  integer", paragraph 1.
- [ADR-1487](1487-upstream-parity-policy-and-guard.md) (upstream parity),
  [ADR-1432](1432-sycl-integer-vif-exact-gain.md) (SYCL gain terms),
  [ADR-1461](1461-strict-fp-every-translation-unit.md) and
  [ADR-1403](1403-cuda-strict-fp-every-kernel.md) (contraction off).
