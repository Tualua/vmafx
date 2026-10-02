<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1467: `ciede.c` writes its squares as products, so `ciede2000` no longer depends on the compiler or on the C library's `powf` for them; a GCC build moves by up to 2e-11

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `numerics`, `ciede`, `clang`, `build`, `gpu-parity`, `testing`, `rc3`, `fork-local`

## Context

A clang build and a GCC build of the CPU `ciede` extractor returned different
`ciede2000` scores on the same machine with the same C library, on x86-64 and
on aarch64 (`T-CIEDE-CLANG-POWF-BUILTIN-2026-10-02`). On ten fixtures (180
frames: the Netflix 576x324 pair at 8, 10, 12 and 16 bits and as 10-bit 4:2:2,
Sparks, both 1920x1080 checkerboard pairs, Big Buck Bunny at 1920x1080 and
3840x2160) 65 frames differed, by at most 1.96e-11.

The cause is one call. `get_r_sub_t()` in `core/src/feature/ciede.c` wrote
`powf(degrees, 2)`. Compiled one form per function with the flags of the
build (`-O3 -std=c23 -ffp-contract=off`; icx with `-fp-model=precise`), the
compilers do this to the power forms of the file:

| Form in `ciede.c` | GCC 16 | clang 22 | icx 2026.0 |
|---|---|---|---|
| `powf(x, 2)` | calls `powf` | multiplies | multiplies |
| `powf(x, 7)` | calls `powf` | calls `powf` | calls `powf` |
| `powf(25., 7)` | constant | constant | constant |
| `pow(x, 2)` (13 uses) | calls `pow` | multiplies | multiplies |
| `pow(x, 7)`, `pow(x, 2.4)`, `pow(x, 1.0 / 3.0)` | calls `pow` | calls `pow` | calls `pow` |
| `pow(25, 7)` | constant | constant | constant |

The file has no power with exponent 0.5; clang leaves `pow(x, 0.5)`,
`pow(x, 3)` and `pow(10.0, x)` as calls in any case, and turns `pow(2, n)`
into `ldexp`, which is exact either way.

So clang replaces a power of two by a product, in `float` and in `double`.
Whether that changes a value depends on the C library (glibc 2.44 here):

- The `float` product is the correctly rounded square. glibc's `powf(x, 2)`
  is not always: it returns the other neighbouring `float` on 4966 of
  4 000 000 sampled arguments of `get_r_sub_t()` (0.12 %), where the exact
  square is a tie. That is the difference between the two builds: a GCC build
  with `degrees * degrees` in the source returns the clang build's values on
  180 of 180 frames.
- The 13 `double` squares are squares of `float` values. Such a square is
  exact in `double` (at most 48 significant bits), and glibc's `pow(x, 2)`
  returns it: equal to the product on 100 million sampled arguments.

Both ways out were measured:

| | Keep the call under clang (`-fno-builtin-powf` for `ciede.c`) | Write the product in `ciede.c` |
|---|---|---|
| GCC build against master | 0 of 180 frames move | 65 of 180 move, at most 1.96e-11 |
| clang build against master | 65 of 180 move, at most 1.96e-11 | 0 of 180 move |
| clang against GCC afterwards | 180 of 180 identical | 180 of 180 identical |
| Netflix golden gate (x86-64 and aarch64, GCC and clang) | 271 passed, 12 skipped | 271 passed, 12 skipped |
| What the value depends on | the C library's `powf` (glibc's and Intel's round ties differently from the product and from each other; others not measured) and a per-file compiler flag | nothing: the product is the correctly rounded square everywhere |
| icx build (Intel's math library) | cannot be made to agree: its `powf(x, 2)` differs from the product on 3041 of 4 000 000 arguments and from glibc's | computes the same expression as every other build |
| GPU twins (CUDA, SYCL, HIP) | the CPU keeps differing from them on this term | the CPU computes what they compute |
| Cost | a clang build 3 % to 9 % slower in `ciede` (three more `powf` calls per pixel) | none; a GCC build makes one `powf` and 13 `pow` calls fewer per pixel and takes about a third less time in `ciede` (563 against 837 ms per 1920x1080 frame on one busy core; clang 517 against 509) |

The first version of this change took the left column, because the GCC build
was to stay bit for bit. The maintainer decided for the right column, provided
the golden gate holds.

## Decision

`ciede.c` writes its squares as products: `degrees * degrees` in
`get_r_sub_t()` and `square(x)`, the `double` product of a `float`, for the 13
uses of `pow(x, 2)` in `ciede2000()`. No compiler argument is involved and
`ciede.c` stays in the feature library. `feature/cuda/integer_ciede/ciede_device.h`
writes the float square as the same product (it rounded the fp64 `pow()` to
`float` before). A GCC build's `ciede2000` moves by up to 2e-11; a clang build
does not move.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| `-fno-builtin-powf` for `ciede.c` under clang, in a library of its own (the first version of this change) | The GCC build does not move by one bit; no source change in a file of Netflix's | See the table above: the value stays a property of the C library, icx cannot follow, the twins keep differing on the term, a clang build pays three `powf` calls per pixel, and a policy block plus two tests exist only to keep one call a call | The maintainer chose the product: popup answer "Write the product, if golden holds (Recommended)" |
| Product for the `float` square only, keep `pow(x, 2)` | Smaller diff | The 13 calls stay compiler-dependent in form (clang folds them, GCC calls) for no difference in value; glibc's `pow(x, 2)` is not the product on 0.08 % of general `double` arguments, so the next libm or the next argument that is not a `float` could differ | Same change, same proof, removes the dependence |
| Also replace `powf(x, 7)` | It is the last float power; with it correctly rounded the CUDA twin equals the CPU on 180 of 180 frames | No compiler folds it, so builds already agree on it; a product chain is not correctly rounded, and a correctly rounded form moves every build again | Not needed for compiler agreement; recorded in `T-CUDA-CIEDE-LIBM-RESIDUAL-2026-10-01` |
| Leave it and document the difference | No change | A clang build and a GCC build of the same source on the same machine disagree | It is the defect |

## Consequences

- **Positive**: x86-64 GCC, x86-64 clang, aarch64 GCC and aarch64 clang
  builds return the same `ciede2000` on 180 of 180 measured frames (115
  between a GCC and a clang build before), at host, AVX2-only and scalar
  dispatch.
- **Positive**: the value of these terms no longer depends on the C library
  or on what a compiler does with a call. clang, MSVC, icx and macOS builds
  compute the same expression as a GCC build.
- **Negative**: `ciede2000` from a GCC build moves on 65 of 180 measured
  frames, by at most 1.96e-11 (Netflix 8-bit pair: frame 35, 6.9e-13; BBB
  1920x1080: 46 of 48 frames; BBB 3840x2160: 16 of 16). The Netflix golden
  gate passes on all four builds (271 passed, 12 skipped): its `ciede`
  assertions hold at their tolerance. No fork snapshot under `testdata/`
  stores `ciede`.
- **Positive**: the GPU twins are closer to the CPU. Against the GCC CPU, on
  the same 180 frames: `ciede_cuda` 127 identical and at most 5.2e-12 (113 and
  2.0e-11 before), `ciede_sycl` 124 and 5.2e-12 (111 and 2.0e-11), `ciede_hip`
  127 and 5.2e-12 (113 and 2.0e-11). What remains is glibc's `powf(x, 7)`:
  against a CPU build with that call correctly rounded (an experiment, not
  shipped) the CUDA twin is identical on 180 of 180 frames, SYCL on 173 and
  HIP on 176 (the rest is the precision of an fp32 pair, at most 8.4e-13).
- **Neutral**: `LIBM_TWINS["ciede"]` stays `1e-9`. The bound is the size of
  one straddling pixel on the smallest gated frame (ADR-1426), and this change
  removes pixels, not their size.
- **Neutral**: `ciede_cuda` itself moves on 3 of 180 frames, by at most
  1.1e-13: on the device `(float)pow((double)x, 2.0)` was not the rounded
  product on every tie.
- **Neutral / follow-ups**:
  - `test_ciede_device_math` replays the CUDA header on the host against the
    CPU extractor, bit for bit. It now holds under every compiler (before, one
    side called `powf(x, 2)` and a compiler was free to fold either call) and
    runs on every architecture; under GCC it fails if `ciede.c` goes back to
    the call.
  - `test_sycl_ciede_exact_contract.py` and `test_cuda_ciede_exact_contract.py`
    pin the new statements.
  - Other integer powers in `core/src` that clang folds and GCC calls:
    `powf(vif_sigma_nsq, 2.0f)` (`vif_tools.c` and its AVX2 and twin-host
    copies), `pow(spatial_frequency / 7, 2)` (`barten_csf_tools.h`) and
    `pow(scores[i] - mean, 2)` (`predict.c`, bootstrap standard deviation).
    A GCC and a clang build agree on all 5472 values measured for them
    (`float_vif` with an option value that is a tie, `adm` and `float_adm`
    with `adm_csf_mode=1`, the bootstrap model; Netflix pair and BBB
    1920x1080), so they are left as they are.

## References

- [ADR-0024](0024-netflix-golden-preserved.md) — the Netflix golden values.
- [ADR-1426](1426-cuda-ciede-cpu-arithmetic.md),
  [ADR-1436](1436-sycl-ciede-cpu-arithmetic.md),
  [ADR-1448](1448-hip-ciede-cpu-arithmetic.md) — the GPU twins and their
  `LIBM_TWINS` bound.
- [ADR-1461](1461-strict-fp-every-translation-unit.md) — no contraction in
  any translation unit.
- `docs/state.md`: `T-CIEDE-CLANG-POWF-BUILTIN-2026-10-02`,
  `T-CUDA-CIEDE-LIBM-RESIDUAL-2026-10-01`.
- Source: popup answer of the maintainer, relayed by the coordinator on 2026-10-02: "Write the product, if golden holds (Recommended)".
- Source: `req` (coordinator, 2026-10-02): "`ciede.c`: `degrees * degrees` in `get_r_sub_t()` instead of `powf(degrees, 2)` (and the double `pow(x, 2)` you found, spelled as the product too if that leaves every value unchanged, as your 100 M-argument check says). Remove the `vmaf_libm_call_args` / `-fno-builtin-powf` policy, its test `test_ciede_libm_call_args.py`, and the separate library if nothing else needs it"
