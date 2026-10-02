<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1476: `ciede2000()` forms its two products in `float`, as Netflix's source does; the `double` casts of PR #552 are removed and the three GPU twins follow

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `numerics`, `ciede`, `upstream`, `gpu-parity`, `testing`, `rc3`, `fork-local`

## Context

The reference for code the fork inherited from Netflix/vmaf is Netflix's
source; a difference needs an ADR (see References). An audit on 2026-10-02
compared every inherited expression with Netflix master at `--precision max`
and found two expressions of `ciede2000()` that differ without one.

Netflix, `libvmaf/src/feature/ciede.c` at `9e48141b`:

```c
const float delta_upcase_h_prime =
        2.0 * sqrt(c_prime_1 * c_prime_2) * sin(delta_h_prime / 2.0);   /* :224-225 */
return sqrt(pow(lightness, 2) + pow(chroma, 2) +
            pow(hue, 2) + r_sub_t * chroma * hue);                      /* :235-236 */
```

All operands of `c_prime_1 * c_prime_2` and of `r_sub_t * chroma * hue` are
`float`, so C evaluates the products in `float` and rounds each to `float`
before the surrounding `double` expression widens it.

The fork had `sqrt((double)c_prime_1 * c_prime_2)` and
`(double)r_sub_t * chroma * hue`. The casts came with PR #552 (2026-05-09), a
sweep that answered the CodeQL alert `cpp/integer-multiplication-cast-to-long`
("multiplication result converted to larger type") by widening the first
operand of 44 products. In an integer product that prevents an overflow. In
these two it changes the value: the product is no longer rounded to `float`.
The Netflix golden gate does not see it (its `ciede` assertions hold at their
tolerance either way).

Measured with the audit's harness (C API, `%.17g`, 31 fixtures from 8x8 to
3840x2160 at 8 to 16 bits, 327 frames with a `ciede2000` on both sides)
against Netflix master built with GCC 16.2.1 and glibc 2.44, scalar
(`cpumask` 63) and default dispatch alike:

| Tree | Identical frames | Largest difference outside 4:2:2 and odd sizes |
|---|---|---|
| master `39929960f` (casts in place) | 7 of 327 | 1.33e-9 |
| this change | 153 of 327 | 2.16e-11 |
| this change with `powf(degrees, 2)` put back (experiment, not shipped) | 272 of 327 | 0 |

The three GPU twins mirror the CPU statement for statement (ADR-1426,
ADR-1436, ADR-1448) and carried the widened products too.

## Decision

`ciede2000()` in `core/src/feature/ciede.c` writes upstream's two
expressions: `sqrt(c_prime_1 * c_prime_2)` and `+ r_sub_t * chroma * hue`,
without a cast. The twins form the same two `float` products:
`cuda/integer_ciede/ciede_device.h` (`chroma_product`, `rotation`) and
`ciede_ff_math.h`, shared by `ciede_sycl` and `ciede_hip`
(`from_float(c_prime_1 * c_prime_2)` and `rotation * chroma * hue` added with
`add_f()`), in place of the exact products. The squares stay products
(ADR-1467). The CodeQL alert on the two lines is answered by an inline
`codeql[...]` comment that cites this ADR, as `iqa/convolve.c` does.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Keep the `double` products and record them as a deviation | No score moves; the exact product is the more accurate one | A deviation needs a reason of its own, and this one has none: it came from a lint sweep, changes 265 of 327 measured frames against upstream, and no test or user depends on it | The maintainer's rule is upstream's expression unless an ADR says why not |
| Upstream's text plus an explicit cast of the product, `sqrt((double)(c_prime_1 * c_prime_2))` | Same value; no clang-tidy suppression for the promotion | The line differs from upstream's for no gain in value, and the file's convention for the same finding (`atan2`, `fabs`, `powf` promotions) is upstream's text with a cited `NOLINT` | Kept the file's convention; the sync story stays "take upstream's side" |
| Also undo ADR-1467 (`powf(degrees, 2)` instead of `degrees * degrees`) | 272 of 327 frames identical to a GCC 16 / glibc 2.44 build of upstream instead of 153 | The value would again depend on the compiler and the C library: clang 22 replaces `powf(x, 2)` by the product, GCC and clang 23.1.1 call the C library (measured on the one expression), and glibc 2.44's `powf(x, 2)` is not the rounded square on 0.12 % of the arguments (ADR-1467). ADR-1467 decided that on 2026-10-02 | Out of scope; ADR-1467 is the recorded deviation for those 119 frames |

## Consequences

- **Positive**: against Netflix master (GCC 16.2.1, glibc 2.44), `ciede2000`
  is identical on 153 of 327 measured frames instead of 7, and every
  remaining difference has a recorded cause:
  - 119 frames, at most 2.16e-11: the `float` square in `get_r_sub_t()` is a
    product since ADR-1467, where upstream calls `powf(degrees, 2)`. With
    that call put back the count is 272 of 327 and these 119 are identical.
    The difference is the `powf` of glibc 2.44: in the dev container (GCC
    15.2, clang 22.1.8, glibc 2.43) a GCC build and a clang build of Netflix
    master both return this fork's values on all 96 frames of the Netflix
    576x324 pair and Big Buck Bunny at 1920x1080, 49 of which differ on the
    glibc 2.44 host.
  - 48 frames, at most 0.153: 4:2:2 input, where the fork upsamples chroma
    with the horizontal flag for columns and the vertical flag for rows and
    upstream has the two swapped (PR #1050,
    `T-BUGHUNT-FEATURE-CPU-2026-06-27`).
  - 7 frames, at most 0.198: 19x19, 17x17 and 12x9 input, where the fork's
    chroma planes are rounded up
    ([ADR-1398](1398-cli-accept-odd-dimensions-chroma-subsampled.md)) and
    upstream's are rounded down.
- **Negative**: `ciede2000` of a CPU build moves on 319 of 327 measured
  frames: by at most 1.3e-9 on frames of 160x90 and larger, by up to 1.0e-8
  on frames of 24x24 and smaller (far below the default `%.6f`). Stored
  `ciede2000` values at `--precision max` differ from new ones by that much.
  No file under `testdata/` stores `ciede`.
- **Neutral**: the twins stay inside their bound,
  `LIBM_TWINS["ciede"] = 1e-9`. Against the GCC CPU extractor at
  `--precision max` on 153 frames (the Netflix 576x324 pair at 8 bits, at 10
  bits and as 10-bit 4:2:2, both 1920x1080 checkerboard pairs, 48 frames of
  Big Buck Bunny at 3840x2160):

  | Twin | Device | Before: identical, largest difference | After |
  |---|---|---|---|
  | `ciede_cuda` | RTX 4090 | 109 of 153, 8.4e-13 | 109 of 153, 2.1e-12 |
  | `ciede_sycl` | Arc A380 (`xe`) | 107 of 153, 7.3e-13 | 107 of 153, 2.1e-12 |
  | `ciede_hip` | gfx1036 | 109 of 153, 7.3e-13 | 108 of 153, 2.1e-12 |

  After the change every differing frame is a 3840x2160 one and the other
  105 frames are identical on all three devices; before it, `ciede_sycl` also
  differed on 2 of the 48 4:2:2 frames, by 7.1e-15. The Metal twin computes in
  `float` throughout and already forms both products in `float`; it is not
  edited and was not run (no device).
- **Neutral**: the SIMD paths of `ciede` only convert samples to `float`;
  scalar, AVX2-only and default dispatch return the same 327 values.
- **Neutral / follow-ups**:
  - `test_ciede_upstream_products` replays `ciede_delta_e()` of the CUDA
    header with the header's own helpers, once with `float` products and once
    with widened ones, over 20 000 colour pairs: the header must return the
    `float` form on every pair, and the pairs must tell the two forms apart
    (193 do). `test_ciede_device_math` holds the header to the CPU extractor
    bit for bit. A cast put back in `ciede.c` alone fails the second test, a
    cast in both files the first, whatever the math library.
  - `test_sycl_ciede_exact_contract.py` and
    `test_cuda_ciede_exact_contract.py` pin upstream's statements in
    `ciede.c` and their mirrors in the two headers, with the widened forms as
    planted regressions.
  - CodeQL reports `cpp/integer-multiplication-cast-to-long` on the two lines
    again. The inline comment names this ADR; the alert is dismissed as
    "won't fix: upstream arithmetic".
  - An upstream sync takes upstream's side of the two expressions.

### Statements of earlier ADRs this replaces

ADR-1426, ADR-1436 and ADR-1448 stay in force: the twins evaluate `ciede.c`'s
arithmetic statement for statement. Their figures were measured against the
CPU with the widened products; the table above replaces them. ADR-1467's
figure "the GPU twins are closer to the CPU: at most 5.2e-12" was measured on
another frame set and is not re-measured here.

## References

- [ADR-1426](1426-cuda-ciede-cpu-arithmetic.md),
  [ADR-1436](1436-sycl-ciede-cpu-arithmetic.md),
  [ADR-1448](1448-hip-ciede-cpu-arithmetic.md) — the twins and their bound.
- [ADR-1467](1467-ciede-squares-as-products.md) — the squares are products;
  the recorded deviation for the `float` square.
- [ADR-0024](0024-netflix-golden-preserved.md) — the Netflix golden values.
- PR #552 (`9ce9ab86a`, "CodeQL C bulk sweep") — where the casts came from.
- Netflix/vmaf `libvmaf/src/feature/ciede.c` at `9e48141b`, lines 224-225 and
  235-236.
- `docs/state.md`: `T-CIEDE-PRODUCTS-NOT-UPSTREAM-2026-10-02`.
- Source: `req` (popup answer, 2026-10-02): "Netflix's source, deviations only by ADR"
