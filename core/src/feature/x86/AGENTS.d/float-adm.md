---
paths:
  - core/src/feature/x86/float_adm_avx2.c
  - core/src/feature/x86/float_adm_avx512.c
  - core/src/feature/adm_tools.c
invariant: Float ADM DWT2 helpers keep one statement per upstream statement; no expression is split or re-ordered.
---
# Float ADM x86 kernels

State 2026-10-02: no dispatch table calls `float_adm_*_avx2` /
`float_adm_*_avx512` (`git grep` finds only their headers; ADR-1057 revert).
No unit test links them. Libraries `x86_float_adm_avx2` /
`x86_float_adm_avx512` build with `vmaf_strict_fp_args`.

## Helper split (ADR-1142, HISS-04)

`float_adm_dwt2_avx2()` / `float_adm_dwt2_avx512()` keep name and signature
(upstream entry points of the port). Row work lives in helpers:

| Helper | Holds |
| --- | --- |
| `dwt2_vertical_row_avx2()` / `dwt2_vertical_row_avx512()` | 4-tap vertical pass of one output row into `tmplo` / `tmphi`: vector loop, then scalar tail |
| `dwt2_horizontal_row_avx2()` | scalar horizontal pass of one row (AVX2 has no vector form here) |
| `dwt2_horizontal_row_avx512()` | `j = 0` scalar, 16-wide loop from `j = 1`, scalar tail |
| `dwt2_horizontal_16_avx512()` | one 16-output group of one input (called for `tmplo`, then `tmphi`) |
| `dwt2_horizontal_scalar()` | one output column, all four bands (used for `j = 0` and the tail) |

Rules for a change here:

- Sum order per output is `((c0*s0 + c1*s1) + c2*s2) + c3*s3`, multiply then
  add, no FMA intrinsic. Same order in vector loop and scalar tail.
- A helper takes whole statements. Never move half an expression into a
  helper, never introduce a temporary of another type (ADR-1253).
- `Dwt2TapsAvx2` / `Dwt2TapsAvx512` carry the broadcast taps; they are set
  once per call, same values as the upstream locals.
- Row pointers use `(ptrdiff_t)i * stride`; indices stay `int`.
- Wiring these kernels into dispatch needs a bit-exact test against
  `adm_dwt2_s()` / `adm_csf_s()` / `adm_csf_den_scale_s()` /
  `adm_sum_cube_s()` first (HISS-15); none exists today.
- Known gap to `adm_dwt2_s()`: sign of zero. Scalar starts each sum at +0
  (`0 + (-0) = +0`), these kernels start at first product (`-0`). Measured
  2026-10-02: identical on picture data, 2.5 % of outputs differ on a
  frame of signed zeros. NEON twin starts at +0. Fix before dispatch.
