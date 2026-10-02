<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1473: The x86 float ADM wavelet and CSF kernels return the scalar bits and are dispatched; the two reduction kernels are removed

- **Status**: Accepted; replaces [ADR-0844](0844-float-adm-avx2-512-f2-f3.md) for the kernels it removes and completes [ADR-1057](1057-revert-float-adm-simd-dispatch-neon-fma.md) on x86
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `metrics`, `adm`, `simd`, `x86`, `correctness`, `performance`, `rc3`, `fork-local`

## Context

`core/src/feature/x86/float_adm_avx2.c` and `float_adm_avx512.c` each held
four kernels of the float ADM pipeline: the wavelet, one band of the CSF
stage, the denominator reduction and a sum of cubes. Since ADR-1057 reverted
the float ADM SIMD dispatch (a NEON kernel had contracted to fused
multiply-add and moved an ARM golden score), nothing called them and no test
linked them. On aarch64 the wavelet came back through a non-contracting NEON
kernel; x86 `float_adm` stayed scalar on every processor.

The standards pass over these files (PR #1864) measured them against the
scalar functions for the first time. None of the four was usable as it stood:

| Kernel | Scalar counterpart | Difference from the scalar bits |
|---|---|---|
| `float_adm_dwt2_*` | `adm_dwt2_s()` | The scalar starts each four-tap sum at `+0` (`accum = 0; accum += c[0] * s0; ...`), so a sum of negative zeros is `+0`. The kernels started at the first product and returned `-0`: 2.5 % of the outputs of a frame of signed zeros. Identical on picture data. |
| `float_adm_csf_*` | the element loop of `adm_csf_s()` | `flt = FLOAT_ONE_BY_30 * fabsf(dst)` is a double product narrowed to float, because the constant is a double literal. The kernels multiplied in float: a different last bit on 0.9 % of values. |
| `float_adm_csf_den_scale_*` | `adm_csf_den_scale_s()` | The scalar adds each cube into a per-row `float` accumulator in column order. The kernels widened the cubes to double and added them in a lane tree. |
| `float_adm_sum_cube_*` | `adm_sum_cube_s()` | The scalar cubes in double; the kernels cubed in float. And `compute_adm()` does not call `adm_sum_cube_s()` at all. |

The maintainer decided the kernels' fate by popup: make them exact, test them
and wire them. A kernel that cannot be made exact is removed.

## Decision

**Wavelet and CSF: exact, tested, dispatched.**

- Every four-tap sum of both wavelet kernels, vector and scalar, starts at
  `+0` and adds one product per step in tap order, with a multiply followed
  by an add (no fused form; the libraries are built with the strict
  floating-point arguments of ADR-1415, and the scalar function carries
  ADR-1057's contraction guard). The AVX2 kernel also filters the horizontal
  pass eight outputs at a time; it was scalar.
- The CSF kernels compute `flt` as a double product narrowed to float.
- `adm.c` chooses the kernel per call from `vmaf_get_cpu_flags()`, as the
  fork's other float extractors do: `adm_dwt2_dispatch()` gains the x86
  branches next to its NEON one, and the CSF stage takes its band kernel
  through `adm_csf_planes_s()`, whose default is the scalar
  `adm_csf_plane_s()`. `--cpumask` selects scalar, AVX2 or AVX-512.
- The wavelet kernels return `int` like `adm_dwt2_s()`: `-ENOMEM` instead of
  leaving the bands unwritten.

**Reductions: removed.** `float_adm_csf_den_scale_avx2()` / `_avx512()` and
`float_adm_sum_cube_avx2()` / `_avx512()` are deleted with their
declarations. The denominator's bits are a sequence of `float` additions in
column order; a kernel that keeps that order adds one value per step and is
bound by the same addition chain as the scalar loop, so it is exact or
faster, not both. The stage is 1.9 % of `float_adm` at 3840x2160. The sum of
cubes has no caller.

`core/test/test_float_adm_x86.c` compares the kernels with the scalar
functions byte for byte and `compute_adm()` across the three dispatch levels.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Exact wavelet and CSF kernels, dispatched; reductions removed (chosen) | `float_adm` 13 to 24 % faster on AVX2 and AVX-512 with every score bit unchanged; no dead kernels left | The denominator reduction stays scalar | Chosen |
| Keep the reduction kernels and make the scalar reference add in eight lanes | A fast denominator | Changes the reference's `float` accumulation, which the Netflix golden scores are taken from (`adm_fold3_s()`'s comment: widening those accumulators changes the scores) | Moves golden values for a stage that is 1.9 % of the time |
| Exact reduction kernels: vector cubes, additions in column order | Exact | The additions are the bottleneck; no gain to measure, more code to keep exact | No benefit |
| Leave all four undispatched | No risk | Dead code that differs from its reference, in the library, untested | The state the standards pass found |
| Delete all four | Smallest tree | Gives up 13 to 24 % of `float_adm` on x86 | The maintainer chose exact and wired |

## Consequences

- **Positive**: `float_adm` is faster on x86 with AVX2 or AVX-512. One
  thread, whole `vmaf` run with `--feature float_adm`, median of five, load
  average 22 to 28 on a Ryzen 9 9950X3D:

  | Frame | scalar (`--cpumask 63`) | AVX2 (`--cpumask 48`) | AVX-512 (`--cpumask 0`) |
  |---|---|---|---|
  | 576x324, before | 1.455 ms | 1.457 ms | 1.421 ms |
  | 576x324, after | 1.450 ms | 1.168 ms | 1.106 ms |
  | 1920x1080, before | 17.53 ms | 16.77 ms | 17.54 ms |
  | 1920x1080, after | 17.90 ms | 15.17 ms | 15.12 ms |
  | 3840x2160, before | 73.86 ms | 73.58 ms | 75.02 ms |
  | 3840x2160, after | 76.16 ms | 62.38 ms | 62.85 ms |

  Before the change the three columns run the same scalar code. The wavelet
  goes from 23 % of the profile to 11 %.
- **Positive**: no score moves. Every `adm` and `float_adm` output under 21
  option sets and the model scores, on 20 fixtures, for scalar, AVX2 and
  AVX-512: 1695 of 1695 recorded cases identical to master, and identical
  across the three dispatch levels.
- **Negative**: the denominator reduction, the decouple (27 % of the profile
  after the change) and the contrast masking (20 %) have no SIMD form;
  `T-FLOAT-ADM-X86-SCALAR-STAGES-2026-10-02` (RC7).
- **Neutral / follow-ups**:
  - Where two different NaNs meet in a sum, the scalar and the vector code
    can return NaNs of different sign or payload (a compiler may commute an
    addition). The test compares NaN positions, not payloads. A NaN in a
    frame fails the frame (ADR-1302).
  - aarch64: `float_adm_dwt2_neon()` is dispatched and starts its sums at
    `+0`; its test covers signed zeros. `float_adm_csf_neon()`,
    `float_adm_csf_den_scale_neon()` and `float_adm_sum_cube_neon()` are
    built, not dispatched, and have the differences this ADR lists for the
    x86 kernels; their test compares them with a reference of its own, not
    with the scalar functions. Same row.

## References

- Maintainer decision (popup answer, 2026-10-02, relayed by the coordinator;
  paraphrased): for the x86 float ADM kernels, make them exact, test them and
  wire them.
- `req` (coordinator brief, 2026-10-02, paraphrased): each sum starts at `+0`
  as `adm_dwt2_s()` does, same order of additions, no fused multiply-add
  unless the scalar contracts identically; a bit-compare test against the
  scalar on picture data, random data, signed zeros, NaN and infinities, at
  sizes around the vector width and the 17x17 minimum, which mutants fail;
  dispatch by CPU flags as the other extractors do; a kernel that cannot be
  made exact is deleted with an RC7 row.
- [ADR-1057](1057-revert-float-adm-simd-dispatch-neon-fma.md),
  [ADR-0844](0844-float-adm-avx2-512-f2-f3.md),
  [ADR-1415](1415-x86-simd-libraries-strict-fp.md),
  [ADR-1442](1442-float-adm-reference-divides.md),
  [ADR-1302](1302-nonfinite-scores-fail-the-frame.md).
- `docs/state.md`: `T-FLOAT-ADM-X86-KERNELS-NOT-EXACT-NOT-DISPATCHED-2026-10-02`,
  `T-FLOAT-ADM-X86-SCALAR-STAGES-2026-10-02`.
