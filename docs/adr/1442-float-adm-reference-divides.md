<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1442: float ADM divides; the processor's reciprocal estimate leaves the reference and the CUDA twin

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `numerics`, `adm`, `cuda`, `gpu-parity`, `upstream-divergence`, `testing`, `rc3`, `fork-local`

## Context

The decouple of float ADM computes `k = t / o` per wavelet coefficient. With
`ADM_OPT_RECIP_DIVISION` (`core/src/feature/adm_options.h`, defined upstream
and here until now) an x86 gcc or clang build forms it as `t * rcp_s(o)`,
`rcp_s(x) = xi + xi * (1 - x * xi)`, `xi = _mm_rcp_ss(x)`. `RCPSS` is specified
by a relative error bound of 1.5 * 2^-12, not bit for bit, and one Newton step
does not remove the dependence: on a Ryzen 9 9950X3D `rcp_s(x)` is not the
fp32 reciprocal for 2.6 million of the 8.4 million mantissas. So `float_adm`
was a function of the processor that ran it, and of the compiler: MSVC and ARM
builds never used the instruction and divide.

[ADR-1420](1420-cuda-float-adm-cpu-arithmetic.md) made `float_adm_cuda` return
the CPU's bits by probing the host's estimate into a table and evaluating the
table on the device. That is exact against the CPU of the same machine only,
and it put a model of one instruction's behaviour into the tree. The row
`T-FLOAT-ADM-RECIPROCAL-ESTIMATE-HOST-DEPENDENT-2026-10-01` left the choice to
the maintainer.

The maintainer chose to divide, provided the Netflix golden gate holds.
Measured before anything else was changed
([Research-1442](../research/1442-float-adm-reference-divides.md)):

- Golden gate (`make test-netflix-golden`'s test selection on the isolated
  golden build): 271 passed, 12 skipped, with the estimate and with the
  division. No assertion moved.
- CPU cost: `float_adm` is not slower. Process CPU time per frame changes by
  -15 % to +7 % across 576x324, 1920x1080 and 3840x2160 and the scalar, AVX2
  and AVX-512 paths on a loaded host, with a median of -9 %.
- Scores: 147 of 791 `float_adm` scores change, by at most 1.3e-7; the
  `vmaf_float_*` models by at most 1.2e-5 per frame and 2.7e-6 per clip mean.

## Decision

Float ADM divides. `adm_options.h` no longer defines
`ADM_OPT_RECIP_DIVISION`; `adm_tools.c` has one `DIVS()`, the plain fp32
quotient, refuses to compile if the macro is defined, and no longer contains
`rcp_s()` or the two exports of the estimate. The scalar code is the only
place float ADM divides (the AVX2, AVX-512 and NEON paths cover the DWT, the
CSF and two reductions, and equal the scalar path bit for bit before and
after), so every x86 build now computes what MSVC and ARM builds already did.

`float_adm_cuda` divides with `__fdiv_rn()`. The host probe, the 4096-entry
table, its upload and `core/src/feature/adm_reciprocal_model.{c,h}` are
deleted. This supersedes the part of ADR-1420 that describes the probed
estimate; the rest of ADR-1420 stands.

## Alternatives considered

| Option | Result | Verdict |
|---|---|---|
| Keep the estimate (upstream behaviour) | Scores stay a property of the processor; a twin can match only its own host, through a probe | Rejected by the maintainer |
| A fixed software table in place of the instruction | Host-independent, but freezes one vendor's low bits as the reference, changes the scores of every other x86 host anyway, and keeps a table and a Newton step nobody can derive from a specification | Rejected |
| **Divide** | One IEEE operation, the same on every host and compiler and on a GPU; x86 scores move by up to 1.3e-7; golden gate holds; not slower | **Chosen** |

## Consequences

- **Positive**: `float_adm` is reproducible to the last bit across x86 hosts
  and between gcc, clang, MSVC and ARM builds, as far as this step is
  concerned. `float_adm_cuda` equals the CPU extractor of any machine, not of
  its own host, and loses 200 lines, a 10 ms probe at start and a warning
  path. Re-proved on an RTX 4090 at `--precision max`: 791 of 791 scores and
  2034 of 2034 outputs with `debug=true` identical, 0 on 200 BBB frames
  through the gate. One more instance of the twin costs 0.83 ms per 3840x2160
  frame instead of 0.98 ms.
- **Negative**: `float_adm` and the `vmaf_float_*` models no longer equal
  upstream Netflix on x86 in the seventh decimal place of the feature (fifth
  to sixth of the model score). Stored x86 scores differ from new ones by the
  amounts above.
- **Neutral / follow-ups**:
  - The fork now differs from upstream in `adm_options.h` and
    `adm_tools.c`. An upstream sync keeps the fork's side
    (`docs/rebase-notes.md`).
  - No fork snapshot under `testdata/` contains a float ADM value; none was
    regenerated.
  - The SYCL, HIP and Metal twins are not ported. They divide already, with
    their device's `/`; their other differences from the reference are in
    `T-GPU-FLOAT-ADM-CPU-ARITHMETIC-2026-10-01`. A SYCL twin built on the
    probed estimate (ADR-1434, in flight) has to drop the probe and the
    table before it lands; the model files it includes no longer exist.
  - The clang-tidy allowance of `adm_options.h` stays at its one baselined
    finding (`#pragma once`): a header's allowance needs a full-lane rewrite
    of the baseline, and the cpu lane currently measures 13 findings above
    its baseline in five files this change does not touch.

## References

- `Q` (popup answer of the maintainer on
  `T-FLOAT-ADM-RECIPROCAL-ESTIMATE-HOST-DEPENDENT-2026-10-01`, 2026-10-02):
  "Divide, if the golden gate holds (Recommended)".
- [ADR-1420](1420-cuda-float-adm-cpu-arithmetic.md) (the probed estimate,
  superseded in that part), [ADR-1317](1317-golden-gate-build-isolation.md),
  [ADR-0024](0024-netflix-golden-preserved.md),
  [ADR-1403](1403-cuda-strict-fp-every-kernel.md),
  [ADR-1428](1428-exact-twins-fragments.md).
- [Research-1442](../research/1442-float-adm-reference-divides.md).
- Intel 64 and IA-32 Architectures Software Developer's Manual, `RCPSS`:
  relative error of the approximation at most 1.5 * 2^-12 (as cited in
  ADR-1420).
