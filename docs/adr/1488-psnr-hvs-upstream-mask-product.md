<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1488: the `psnr_hvs` masking threshold is the `double` root of a `float` product, as Netflix's source writes it; the `double` product of PR #552 is removed and the AVX2, NEON, CUDA, HIP and SYCL forms follow, bit for bit

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `numerics`, `psnr-hvs`, `upstream`, `simd`, `gpu-parity`, `testing`, `rc3`, `fork-local`

## Context

The reference for code the fork inherited from Netflix/vmaf is Netflix's
source; a difference needs an ADR (see References). An audit on 2026-10-02
compared every inherited expression with Netflix master at `--precision max`
and found the masking threshold of `psnr_hvs` to differ without one.

Netflix, `libvmaf/src/feature/third_party/xiph/psnr_hvs.c` at `9e48141b`,
lines 316-317:

```c
s_mask = sqrt(s_mask * s_gvar) / 32.f;
d_mask = sqrt(d_mask * d_gvar) / 32.f;
```

`s_mask` and `s_gvar` are `float`, so C evaluates the product in `float` and
rounds it to `float` before `sqrt()` widens it. The root is taken in `double`
and the result is stored as `float`.

The fork had `sqrt((double)s_mask * s_gvar)`. The cast came with PR #552
(2026-05-09), a sweep that answered the CodeQL alert
`cpp/integer-multiplication-cast-to-long` by widening the first operand of 44
products. Here it keeps the product exact, and the threshold comes out one
`float` step away on about one block in twenty of real content. The Netflix
golden gate does not see it.

Everything built since then copied the cast, each time on purpose and each
time recorded as "the CPU's arithmetic":

- `x86/psnr_hvs_avx2.c` (comment: "ADR-0138 bit-exactness fix");
- `arm64/psnr_hvs_neon.c`, which still had upstream's `float` product until
  PR #1851 changed it to the cast on 2026-10-02
  (`T-PSNR-HVS-NEON-NOT-SCALAR-BITS-2026-10-02`, with ADR-1469);
- the CUDA and HIP kernels (ADR-1397, ADR-1401), whose threshold took a
  `double` product and a `double` square root;
- the SYCL kernel (ADR-1401), which has no `double` and got
  `sqrt_prod_rn()`, an integer product and integer square root, to reproduce
  the exact product.

Measured with the audit's harness (C API, `%.17g`, 319 frames from 8x8 to
3840x2160 at 8 to 12 bits in every chroma layout) against Netflix master
built with GCC 16.2.1 and glibc 2.44:

| Output | master `39929960f`: identical frames, largest difference | This change |
|---|---|---|
| `psnr_hvs` | 292 of 319, 7.9e-7 | 319 of 319 |
| `psnr_hvs_y` | 310 of 319, 9.4e-7 | 319 of 319 |
| `psnr_hvs_cb` | 310 of 319, 5.0e-7 | 319 of 319 |
| `psnr_hvs_cr` | 309 of 319, 5.7e-7 | 319 of 319 |

The figures are the same with every instruction-set flag masked and with the
host's dispatch; with AVX2 alone (271 frames) `psnr_hvs` goes from 246 to 271.

## Decision

`calc_psnrhvs()` writes upstream's two statements, without a cast. Every
other form of the threshold returns the same bits:

| Where | Threshold |
|---|---|
| `third_party/xiph/psnr_hvs.c` | `sqrt(s_mask * s_gvar) / 32.f` (upstream's text) |
| `x86/psnr_hvs_avx2.c`, `arm64/psnr_hvs_neon.c` (`compute_masks()`) | `(float)(sqrt(b->s_mask * b->s_gvar) / 32.0)` |
| `cuda/integer_psnr_hvs/psnr_hvs_score.cu`, `hip/integer_psnr_hvs/psnr_hvs_score.hip` (`hvs_threshold()`) | `product = energy * ratio` in `float`, then `(float)(sqrt((double)product) / 32.0)` |
| `sycl/integer_psnr_hvs_sycl.cpp` (`hvs_threshold()`) | `sqrt_rn(energy * ratio) / 32.f` |

The SYCL kernel has no `double` (ADR-0220). It does not need one: a correctly
rounded `float` square root of a `float` equals the `double` root rounded to
`float`, because rounding a square root to 53 bits and then to 24 never
differs from rounding it to 24 at once (53 >= 2 * 24 + 2). `sqrt_rn()` of
`sycl_exact_fp.h` is that root, and division by 32 only changes the exponent.
The equality was checked on all 2 139 095 039 positive finite `float` values
on the host (`(float)(sqrt((double)p) / 32.f)` against `sqrtf(p) / 32.f`, the
first with a real `double` root): no mismatch.

`sqrt_prod_rn()` and `isqrt_floor50()` leave `sycl_exact_fp.h`: nothing else
used them. That undoes a part of commit `90aa3f619` on purpose, so
`scripts/ci/silent-revert-allowlist.json` declares it for the Silent-Revert
Guard, with this ADR as the decision.

The twins stay exact twins: no fragment under `scripts/ci/exact_twins.d/`
changes and no tolerance is introduced.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Keep the `double` product and record it as a deviation | No score moves; five files and their tests stay as they are; the exact product is the more accurate one | The deviation has no reason of its own: it came from a lint sweep. It makes 27 of 319 measured frames differ from upstream, and the SYCL twin needs an integer square root of 25 steps per block to follow it | The maintainer's rule is upstream's expression unless an ADR says why not |
| Upstream's expression on the CPU only, twins within a tolerance | Smaller change | Three exact twins would leave `exact_twins.d`, against the RC3 contract | Every twin can return the new value bit for bit, so there is no reason to give up exactness |
| SYCL: keep an integer root, of the rounded product | No reliance on the device's square root | `sqrt_rn()` already is a correctly rounded root, checked against the host on the device by `test_sycl_fp_arith_contract`; a second root for one caller is a second implementation | One root, already in the header |
| CUDA / HIP: `sqrtf()` of the product | Shorter; the same value by the argument above | The kernels follow the CPU statement for statement, and the statement takes the root in `double` | Kept the reference's form |

## Consequences

- **Positive**: `psnr_hvs`, `psnr_hvs_y`, `psnr_hvs_cb` and `psnr_hvs_cr` are
  identical to Netflix master on all 319 measured frames, scalar, AVX2 and
  default dispatch. What still differs from Netflix's extractor is behaviour:
  the fork refuses input above 12 bits where upstream returns nothing, and
  scores the luma plane of 4:0:0 input where upstream refuses it.
- **Negative**: scores of this fork move on 27 of the 319 frames, by at most
  9.4e-7 dB in a plane score and 7.9e-7 dB in `psnr_hvs`. A value printed
  with the default `%.6f` can change in its last digit. No file under
  `testdata/` stores `psnr_hvs`.
- **Positive**: the SYCL kernel loses an integer square root of 25 steps per
  work-item. Its timing was not measured here
  (`T-SYCL-HIP-PSNR-HVS-EXACT-SUM-THROUGHPUT-2026-10-01` tracks the twin's
  cost).
- **Neutral**: the three twins are exact, as before. On an RTX 4090, a
  gfx1036 and an Arc A380 (`xe`), 612 of 612 values are identical to the CPU
  extractor of the same binary at `--precision max` (153 frames, four
  outputs: the Netflix 576x324 pair at 8 bits, at 10 bits and as 10-bit
  4:2:2, both 1920x1080 checkerboard pairs, 48 frames of Big Buck Bunny at
  3840x2160). The parity gate cell is at tolerance 0 with a largest
  difference of 0 on all three, and `test_{cuda,hip,sycl}_psnr_hvs_parity`,
  their `_large` forms and `test_{cuda,hip,sycl}_exact_twins` pass. The Metal
  twin already forms the product in `float`; it is not edited and was not run
  (no device).
- **Neutral**: the SYCL kernel stays free of scratch memory
  (`test_sycl_kernel_scratch`: 128 kernels audited, none uses any) and
  compiles for all 19 targets of the default list (`--suite sycl-aot`).
- **Neutral / follow-ups**:
  - `test_psnr_hvs_dispatch_invariance` gains
    `test_scalar_scores_are_upstreams`: twelve recorded 8x8 blocks of the
    Netflix pair, each of which the two products score differently, must
    return the scores Netflix master returns for them (checked against a
    build of `9e48141b`; master before this change misses all twelve).
  - `test_psnr_hvs_simd` compares the AVX2 function with a copy of the scalar
    one that now carries upstream's statement.
  - `test_psnr_hvs_twin_exact_sum_contract.py` pins the statement in the
    scalar file, in both SIMD files and in the three kernels, with the cast
    and the exact product as planted regressions.
  - `test_sycl_fp_arith_contract` runs `sqrt_rn()` of the `float` product on
    the device against the host's statement, on 1.3 million operand pairs,
    among them the products nearest a rounding boundary of their root.
  - CodeQL reports `cpp/integer-multiplication-cast-to-long` on the two lines
    again. The inline comment names this ADR; the alert is dismissed as
    "won't fix: upstream arithmetic".
  - An upstream sync takes upstream's side of the two statements.

### Statements of earlier ADRs this replaces

Accepted ADRs are not edited; these statements of theirs no longer hold:

- **ADR-1397** (CUDA), Decision: "the masking threshold is
  `sqrt((double)mask_energy * variance_ratio) / 32` rounded to `float` once".
  Replaced by: `sqrt(mask_energy * variance_ratio) / 32` with the product
  rounded to `float` first, as upstream writes it. The rest of ADR-1397
  stands (terms stored per block, one running `float` on the host,
  `--fmad=false`).
- **ADR-1401** (SYCL, HIP), title and Decision: "the fp64-free masking
  threshold is an integer square root", the HIP threshold "from a `double`
  product and square root", and the SYCL threshold "from a new
  `sqrt_prod_rn(a, b)`". Replaced by: HIP forms the `float` product and takes
  the `double` root; SYCL takes `sqrt_rn()` of the `float` product;
  `sqrt_prod_rn()` is removed. The rest of ADR-1401 stands.
- **ADR-1469** (NEON) names its occasion as "fixing the masking threshold of
  `calc_psnrhvs_neon()`": PR #1851 gave the NEON function the scalar's
  `double` product (`T-PSNR-HVS-NEON-NOT-SCALAR-BITS-2026-10-02`). The NEON
  function now has the scalar's `float` product, which is the statement it
  had before that fix. The rule that fix applied stands and is applied here:
  the SIMD functions return the scalar reference's bits. ADR-1469's decision,
  the two-stage butterfly, is untouched.
- **ADR-0138**, as `psnr_hvs_avx2.c` cited it for the cast: the AVX2 function
  matches the scalar reference, and the reference's statement is upstream's.

## References

- [ADR-1397](1397-psnr-hvs-twins-cpu-float-sum.md),
  [ADR-1401](1401-psnr-hvs-sycl-hip-exact-twins.md) — the exact twins.
- [ADR-1469](1469-psnr-hvs-simd-butterfly-two-stages.md),
  [ADR-0159](0159-psnr-hvs-avx2-bitexact.md),
  [ADR-0160](0160-psnr-hvs-neon-bitexact.md) — the SIMD functions.
- [ADR-1395](1395-sycl-kernels-no-scratch.md),
  [ADR-0220](0220-sycl-fp64-fallback.md) — SYCL kernels without scratch
  memory and without fp64.
- [ADR-0024](0024-netflix-golden-preserved.md) — the Netflix golden values.
- PR #552 (`9ce9ab86a`, "CodeQL C bulk sweep") — where the cast came from.
- Netflix/vmaf `libvmaf/src/feature/third_party/xiph/psnr_hvs.c` at
  `9e48141b`, lines 316-317.
- `docs/state.md`: `T-PSNR-HVS-MASK-PRODUCT-NOT-UPSTREAM-2026-10-02`.
- Source: `req` (popup answer, 2026-10-02): "Netflix's source, deviations only by ADR"
