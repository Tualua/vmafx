<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1475: The quantisation step of integer ADM is Netflix's expression again, so `vmaf_v0.6.1` returns Netflix master's bits

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `numerics`, `adm`, `netflix-compat`, `upstream-divergence`, `codeql`, `sycl`, `metal`, `gpu-parity`, `testing`, `fork-local`

## Context

The maintainer decided on 2026-10-02 what the reference for inherited code is:
Netflix's source. Every expression the fork inherited from Netflix/vmaf
evaluates as Netflix master evaluates it, unless an ADR records why the fork
differs.

An audit of the fork's CPU against Netflix master (`cea2b4d8`, both built with
GCC 16, release, x86-64; every emitted metric read through the C API at
`%.17g`, 31 fixtures, scalar, AVX2 and AVX-512 dispatch) found that the fork's
integer ADM differed from Netflix's on every fixture: `adm2` on 606 of 658
frames by up to 8.5e-8 on decoded pictures, `adm3` on 654, and with them every
model that reads an integer ADM score. `vmaf_v0.6.1` differed on 472 of 504
frames by up to 1.83e-5, `vmaf_v0.6.1neg` by 1.81e-5, `vmaf_4k_v0.6.1` by
1.49e-5, the bootstrap model `vmaf_b_v0.6.3` by 2.1e-5. The Netflix golden
assertions compare at four or five decimals and pass either way, which is why
it went unnoticed; `T-UPSTREAM-AB-SCORE-DELTA-2026-09-07` had recorded the
symptom on the pooled score (4e-6 on the 1 px checkerboard pair) without a
cause, and `T-ADM-CSF-EXPONENT-NOT-UPSTREAM-2026-10-01` had found the
expression and asked for a decision.

The cause is one cast. `dwt_quant_step()` computes the Watson quantisation
step of a wavelet band, from which the CSF weights of all four scales follow.
It raises 10 to `k * temp * temp`. Netflix
(`libvmaf/src/feature/integer_adm.c`, `dwt_quant_step()`) multiplies the three
`float`s in `float` and promotes the product for `pow()`:

```c
float Q = 2.0*params->a*pow(10.0, params->k*temp*temp) / ...
```

PR #552 (`9ce9ab86a`, 2026-05-09), a sweep that cleared 60 static-analysis
findings, wrote `params->k * (double)temp * temp` to silence
`cpp/integer-multiplication-cast-to-long`. That makes the product a `double`
product. The weights moved by one to three units in the last place (scale 0:
`0x1.1cc772p-6` against Netflix's `0x1.1cc774p-6`). Scale 0 converts its
weights to 16-bit fixed point, where both round to the same integers; scales 1
to 3 convert to 32 bits, so their contrast-masking and CSF sums differ.

The change was unintended in the sense of the decision above: the pull request
was a lint sweep, it has no ADR, it states no measurement, and nothing in the
fork needed the wider product.

## Decision

`dwt_quant_step()` in `core/src/feature/integer_adm_kernels.h` is Netflix's
expression again: `pow(10.0, params->k * temp * temp)`. The static-analysis
finding is answered with a suppression that cites this ADR, on the line above.
The CUDA and HIP hosts take the function from that header. The two twins that
carry their own copy change with it: `core/src/feature/sycl/integer_adm_sycl.cpp`
and `core/src/feature/metal/integer_adm_metal.mm` form the exponent as a
`float` product in a named `float` and promote that.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Keep the `double` product and record it here as a deliberate deviation | No score moves | The fork's default model differs from Netflix's on 94 % of frames for no reason anyone chose; a deviation needs a reason | There is none: it came from a lint sweep |
| Restore the `float` product and drop the suppression | Shortest diff | The finding returns and the next sweep "fixes" it the same way | That is how it happened |
| Give the SYCL and Metal twins the CPU's header instead of a copy | One implementation | The header is C with designated and positional initialisers a C++ and an Objective-C++ translation unit do not take as they are; a larger change than the defect | Out of scope here; `test_integer_adm_quant_step_contract.py` reads all three copies instead |

## Consequences

- **Positive**: on the audit's fixtures the fork's integer ADM is Netflix's on
  every frame of every decoded picture, at scalar, AVX2 and AVX-512 dispatch
  (`adm2`: 616 of 658 frames identical at scalar dispatch, 636 at AVX2, 632 at
  AVX-512; 52 before). The frames that still differ are the deliberate
  deviations: four synthetic noise fixtures, where the fork keeps the scale-0
  masking centre tap in int32 (ADR-1402) and Netflix's scalar and vector paths
  disagree with each other, and frames of 17 to 24 pixels, where Netflix reads
  outside the band.
- **Positive**: every model that reads only integer features returns Netflix
  master's score on every measured frame at AVX2 and AVX-512 dispatch
  (`vmaf_v0.6.1`, `vmaf_v0.6.1neg`, `vmaf_4k_v0.6.1`, `vmaf_4k_v0.6.1neg`,
  `vmaf_b_v0.6.3`, `vmaf_v0.6.1mfz`; for `vmaf_v0.6.1` 504 of 504 frames, 32
  before, at most 1.83e-5). At scalar dispatch the six frames of the 8-bit
  noise fixture remain (ADR-1402).
  `testdata/bench_upstream_ab.py --max-score-delta 0` passes; it failed on the
  1 px checkerboard pair (4e-6) and on Big Buck Bunny at 3840x2160 (1e-6).
- **Negative**: every integer ADM score and every score of a model that reads
  one moves, by the amounts above. The five `testdata/scores_cpu_*.json`
  snapshots move on 38 to 59 of their 720 values each, by at most 2e-5 (the
  `vmaf` column), and are regenerated.
- **Neutral**: the Netflix golden gate passes before and after (271 passed, 12
  skipped, x86-64 and aarch64).
- **Neutral**: the exact twins stay exact. `adm_cuda` and `adm_hip` read the
  header; `adm_sycl` and `adm_metal` have the same edit. On an RTX 4090, a
  gfx1036 and an Arc A380 the parity tests and the gate's `adm` cells hold at
  tolerance 0. The Metal copy is not run (no device).
- **Neutral**: a build with Intel's compiler links Intel's math library, whose
  `powf` rounds a few arguments differently from glibc's. With the weights of
  this change one of 240 measured frames lands on such an argument (Big Buck
  Bunny at 1280x720, frame 26: `integer_adm_scale1` differs by 7.9e-8, `vmaf`
  by 2.8e-6 between an icx build and a GCC build); before, none of the 240
  did. The same icx-built library with glibc's `libm` loaded returns the GCC
  build's values on all 48 frames. This is the dependence on the math library
  that [build flags](../development/build-flags.md) describes, not a property
  of this expression: the step itself has the same bits under both libraries.
- **Replaces**: the statement in
  [ADR-1416](1416-cuda-adm-cpu-row-rounding.md) that the CPU "evaluates the
  exponent `k * temp * temp` in `double`", and that ADR's follow-up "the CPU's
  own CSF exponent differs from upstream Netflix since #552". Both described
  the tree of their day; the CPU evaluates it in `float` now. ADR-1416's
  decision (the CUDA host takes the weights from the CPU's header) stands.
- **Follow-up**: float ADM carries the same step in `adm_tools.h`, widened
  further by a later port; it gets its own change and ADR.

## References

- `req` (popup answer, 2026-10-02): "Netflix's source, deviations only by ADR"
- [ADR-1416](1416-cuda-adm-cpu-row-rounding.md),
  [ADR-1402](1402-adm-cm-centre-tap-int32.md),
  [ADR-1228](1228-upstream-ab-perf-milestone.md)
- [Research-1475](../research/1475-integer-adm-quant-step-upstream-float.md)
- Netflix/vmaf `cea2b4d8`, `libvmaf/src/feature/integer_adm.c`,
  `dwt_quant_step()`
- PR #552 (`9ce9ab86a`)
