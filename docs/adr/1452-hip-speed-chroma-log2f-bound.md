<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1452: the gate bounds `speed_chroma_hip` against the CPU by what glibc's `log2f` adds, as it does for the CUDA twin

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `hip`, `gpu-parity`, `numerics`, `speed`, `testing`, `ci`, `rc3`, `fork-local`

## Context

`speed_chroma_hip` runs `speed.c`'s arithmetic on the device and rounds
`log2` correctly (`speed_hd_log2_rn()`,
[ADR-1384](1384-hip-speed-device-resident.md)). `speed.c` calls the C
library's `log2f`, which glibc does not round correctly.
[ADR-1430](1430-cuda-speed-chroma-log2f-bound.md) measured what that does to
the CUDA twin and gave its gate cell a bound of `5e-6` in `LIBM_TWINS`. The
HIP cell stayed at the general `5e-5`, and its C test compared one score of
one frame within `1e-4` on a 768x432 fixture whose covariance is singular, so
the scoring path with its `log2` never ran.

Measured on a gfx1036 (ROCm 7.2.4, glibc 2.44) at `--precision max`,
`--backend hip --feature speed_chroma_hip` against `--backend cpu --feature
speed_chroma`, and against the same CPU binary with a correctly rounded
`log2f` preloaded (`LD_PRELOAD`, `(float)log2((double)x)`), values identical
over the three scores of every frame:

| Fixture | Frames | Twin and CPU | Twin and CPU with the preload |
|---|---|---|---|
| Netflix 576x324, 8 bit | 48 | 142 of 144 | 144 of 144 |
| BBB 3840x2160 | 200 | 589 of 600 | 600 of 600 |
| Netflix 576x324 at 10, 12 and 16 bit | 9 | 27 of 27 | 27 of 27 |
| Netflix 576x324, 10-bit 4:2:2 | 48 | 144 of 144 | 144 of 144 |
| Both 1920x1080 checkerboard pairs | 6 | 18 of 18 | 18 of 18 |
| Sparks 480x270, 10 bit | 5 | 15 of 15 | 15 of 15 |
| Full-range noise 576x324 at 8, 10, 12 and 16 bit | 12 | 36 of 36 | 36 of 36 |
| Bright 16 bit, 1920x1080 | 2 | 6 of 6 | 6 of 6 |

977 of 990 values equal the CPU's and 13 differ, by 1.431e-6 at most. With
the preload all 990 are identical, and the preloaded CPU run differs from the
plain one on exactly those 13 values by exactly those amounts. They are the
13 values of the CUDA twin: Netflix frame 3 (`_v` 1.192e-6, `_uv` 9.537e-7),
BBB frames 17, 21, 103, 138, 150 and 191. No source of the twin is involved.

## Decision

The twin does not change. `LIBM_TWINS["speed_chroma"]` lists `hip` at `5e-6`
next to `cuda`, with ADR-1430's derivation: a difference is a whole number of
steps of the fp32 score, the largest count measured is five, the gate's
fixtures score below 16 where a step is at most 2^-20, and five such steps
are 4.77e-6. The cell runs at `--precision max`. A cell between the CUDA and
the HIP twin resolves to the same bound.

`test_hip_speed_chroma_parity` compares all three scores of every frame to
one part in a million of the CPU's score on the 960x960 textured fixture of
the CUDA test, whose covariance is regular. The fixture, the CPU run and the
comparison move to `core/test/speed_chroma_twin_parity.h`, which both tests
wrap. On it the HIP twin is 3.8e-6 from the glibc CPU on two values of one
frame (two steps at a score of 22.5, 1.7e-7 of it), the CUDA twin's figures.

`T-HIP-SPEED-CHROMA-GLIBC-LOG2F-2026-10-02` stays open, because the cell is
not `0`.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| List `hip` at `5e-6` (this ADR) | The cell is ten times tighter and its cause is named; one bound for both twins that round `log2` correctly | Not exact | Chosen |
| Keep the general `5e-5` | No change | A regression of the twin up to `5e-5` passes the gate; its C test never reached the scoring path | The residual is attributed completely and is 35 times smaller |
| Declare the twin exact | Tolerance 0 | Fails on a glibc build: 13 of 990 values differ | The CPU side would have to change first |
| Evaluate glibc's `log2f` on the device, or give `speed.c` a `log2f` with defined rounding | The cell could become exact | The first ties the twin to one host library (rejected in ADR-1430); the second changes the CPU reference and belongs to `T-ICX-LIBIMF-HOST-MATH-2026-10-01` | Outside a twin lane |

## Consequences

- **Positive**: the HIP `speed_chroma` cell is gated at a bound 10 times
  tighter than before, and `test_hip_speed_chroma_parity` reaches the scoring
  path for the first time.
- **Negative**: none in the product; no source of the twin changes and no
  score changes. The cell is not exact and the row stays open.
- **Neutral / follow-ups**:
  - The absolute bound does not fit content whose scores exceed 16; such a
    fixture needs it scaled with the float step of its scores (ADR-1430).
  - The 13 values belong to the host's `log2f`; another glibc version has
    another set of the same kind.
  - `speed_chroma_sycl` is not listed; it is another lane's cell.

## References

- `req` (coordinator brief for the HIP lane, 2026-10-02): "`speed_chroma_hip`: add the measured `hip: 5e-6` to `LIBM_TWINS` with the test and the row update you already have evidence for (same shape as CUDA's ADR-1430 / #1751); tiny PR."
- [ADR-1430](1430-cuda-speed-chroma-log2f-bound.md),
  [ADR-1384](1384-hip-speed-device-resident.md),
  [ADR-1426](1426-cuda-ciede-cpu-arithmetic.md),
  [ADR-1421](1421-rc3-rc8-candidate-map.md),
  [ADR-0214](0214-gpu-parity-ci-gate.md).
- [Research-1430](../research/1430-cuda-speed-chroma-log2f-bound.md) (the
  preload recipe).
- `docs/state.md`: `T-HIP-SPEED-CHROMA-GLIBC-LOG2F-2026-10-02`,
  `T-CUDA-SPEED-CHROMA-GLIBC-LOG2F-2026-10-01`.
