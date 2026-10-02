<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1460: `speed_temporal` becomes a parity-gate feature with a derived bound, and every registered twin of a gated backend has to be a gate cell

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `ci`, `gpu-parity`, `numerics`, `cuda`, `hip`, `sycl`, `testing`, `rc3`, `fork-local`

## Context

The cross-backend parity gate
(`scripts/ci/cross_backend_parity_gate.py`) compares a GPU twin with its CPU
extractor for the features in `FEATURE_METRICS`. A registered twin whose
feature is not in that table is selectable with `--backend` and guarded by
its own unit test only. Comparing the extractor registry
(`core/src/feature/feature_extractor.cpp`) with the table showed:

- `speed_temporal_cuda`, `speed_temporal_hip` and `speed_temporal_sycl` are
  registered and `speed_temporal` was no gate feature. Every other CUDA, SYCL
  and HIP twin (18 of 19 per backend) is the extractor of some gate feature.
- The 17 Metal twins are registered and the gate has no `metal` backend at
  all.
- Two more holes of the same kind, in what a cell compares: the gate's `psnr`
  cell listed `psnr_y` only, where the CPU and every twin emit three planes;
  and the single-feature gate (`scripts/ci/cross_backend_vif_diff.py`), which
  carries its own copy of the tables, had no `ssim` feature.

`speed_temporal` is the second score of `speed.c`. Its twins run the device
chain of `speed_chroma` (ADR-1380 for CUDA, ADR-1384 for HIP, ADR-1358 for
SYCL), which rounds `log2` correctly, where `speed.c` calls the C library's
`log2f`. Measured at `--precision max` against the CPU extractor of each
twin's own build, which is what the gate compares (GCC and glibc 2.44 for
CUDA and HIP, icx for SYCL), and on BBB against named CPU runs:

| Fixtures | CUDA (RTX 4090) | HIP (gfx1036) | SYCL (Arc A380) |
|---|---|---|---|
| Typical: Netflix 576x324 at 8 and 10 bit, both 1080p checkerboards | 57 of 57 | 57 of 57 | 57 of 57 |
| Stress: Netflix at 12 and 16 bit and as 10-bit 4:2:2, Sparks, noise at 8, 10, 12 and 16 bit, bright 16-bit 1080p | 73 of 73 | 73 of 73 | 73 of 73 |
| BBB 1080p and 3840x2160 widened to 16 bit | 72 of 72 | not run | not run |
| BBB 3840x2160, 104 frames, glibc 2.44 CPU | 102 of 104, 4.8e-7 | 102 of 104, 4.8e-7 | 102 of 104, 4.8e-7 |
| the same 104 frames, CPU with a correctly rounded `log2f` preloaded | 104 of 104 | 104 of 104 | 104 of 104 |
| the same 104 frames, the CPU of an icx build (Intel's `log2f`) | — | — | 104 of 104 |
| BBB 3840x2160, 200 frames, glibc 2.44 CPU | 198 of 200, 4.8e-7 | not run | not run |

The three twins return the same bits on all 104 frames. The glibc CPU
differs from them on frames 100 and 102, by one step of the fp32 score at
6.58 and 6.83, and moves onto them when its `log2f` rounds correctly. That
is the finding of ADR-1430 for `speed_chroma`: nothing separates twin and CPU
but the host's `log2f`.

## Decision

We will make `speed_temporal` a gate feature (one metric, `speed_temporal`),
in both gates, and list it in `LIBM_TWINS` for `cuda`, `hip` and `sycl` at
`4e-5`, compared at `--precision max`.

The bound is ADR-1430's count in this score's step. A difference is a whole
number of float steps of the score; the largest count measured for this
mechanism is five (on `speed_chroma`; one on `speed_temporal`). On the gate's
fixtures `speed_temporal` reaches 41 (Netflix pair), 69 (1080p checkerboard)
and 84 (full-range noise). Below 128 a step is at most 2^-17, and five steps
are 3.8e-5, written as `4e-5`. A fixture scoring above 128 needs the same
count in its own step.

`core/test/test_parity_gate_covers_registered_twins.py` reads the registry
and the gate's tables and fails when a registered twin of a backend the gate
runs is no gate feature's extractor, when a backend with registered twins is
neither a gate backend nor on record with a state row, or when the two
gates' tables differ. The `psnr` cell compares all three planes, and the
single-feature gate gets `ssim`.

The gate does not get a `metal` backend here: no Apple device runs it on this
project's hosts. `T-GATE-NO-METAL-BACKEND-2026-10-02` records the 17 twins.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Gate feature with a bound of five steps below 128 (this ADR) | The cell exists, runs at `--precision max`, and its tolerance has a derivation; 80 times the largest measured difference, below the places=4 default | Barely tighter than `5e-5` | Chosen |
| The `5e-6` of `speed_chroma` | Ten times tighter | Sized for scores below 16; `speed_temporal` scores 14 to 84 on the gate's own fixtures, where one float step is up to 7.6e-6, so a single step would fail the cell | Not valid for this score's range |
| One float step at the measured scores (`5e-7`) | Tight on BBB | A one-step difference at a score of 70 is 7.6e-6 | The count is what the mechanism bounds, not the absolute value |
| List the twins as exact | Tolerance 0 | True against an icx CPU, false against glibc (2 of 104 BBB frames) | Depends on the host math library |
| Leave `speed_temporal` out, keep the twins' unit tests | No change | The twins stay outside the one matrix that runs every twin; a regression below their unit tests' `1e-4` passes | The reason for this change |
| A relative tolerance in the gate | The natural form of "n float steps" | A new tolerance kind in both gates for one feature | Not needed while one absolute bound covers the fixtures |
| Give `speed.c` a `log2f` with defined rounding | Twins exact on every host | Changes CPU scores of an extractor ported from upstream; outside this change | `T-ICX-LIBIMF-HOST-MATH-2026-10-01` tracks the host-library dependence |

## Consequences

- **Positive**: every registered CUDA, SYCL and HIP twin is a gate cell, and a
  test keeps it so.
- **Positive**: the `psnr` cells compare `psnr_cb` and `psnr_cr` too. On the
  fixtures above they are identical on CUDA, HIP and SYCL.
- **Negative**: `speed_temporal`'s bound is loose in absolute terms. It says
  "at most five float steps of a score below 128", not "within 4e-5 of the
  CPU's real value at any score".
- **Neutral / follow-ups**:
  - `speed_chroma` on SYCL stays at the places=4 default; ADR-1451 left it
    unlisted for the same library dependence, and listing it in `LIBM_TWINS`
    at `5e-6` would be the consistent next step.
  - The two gates keep separate copies of `FEATURE_METRICS`,
    `FEATURE_ALIASES` and `BACKEND_EXTRACTOR_ALIASES`; the new test holds
    them equal. One definition is work for the deduplication phase.
  - The gate's `motion` cells list `integer_motion2` and `integer_motion3`,
    not the SAD score every frame carries
    (`T-GPU-MOTION-SAD-SCORE-NOT-EMITTED-2026-10-02`: the SYCL twin does not
    emit it yet).

## References

- `req` (coordinator brief for the CUDA lane, 2026-10-02): "`speed_temporal` into the parity gate: a twin that is not a gate feature is unguarded. Add it to `FEATURE_METRICS` (+ tolerance / `LIBM_TWINS` entry with a measured bound like speed_chroma's, docs row in `docs/development/cross-backend-gate.md`) for CUDA, measured on the typical + stress sets."
- `req` (same brief): "Also list any other registered GPU twin that `FEATURE_METRICS` does not cover (compare the extractor registry with the gate's table) and add or row each."
- [ADR-1430](1430-cuda-speed-chroma-log2f-bound.md),
  [ADR-1452](1452-hip-speed-chroma-log2f-bound.md),
  [ADR-1426](1426-cuda-ciede-cpu-arithmetic.md),
  [ADR-1451](1451-sycl-exact-twins-declared.md),
  [ADR-1380](1380-cuda-speed-device-resident-pipeline.md),
  [ADR-1384](1384-hip-speed-device-resident.md),
  [ADR-1358](1358-sycl-speed-device-resident-linalg.md),
  [ADR-1418](1418-motion-parity-gate-metric-alignment.md),
  [ADR-0214](0214-gpu-parity-ci-gate.md).
- `docs/state.md`: `T-GATE-SPEED-TEMPORAL-UNGATED-2026-10-02` (opened and
  closed by this decision), `T-GATE-NO-METAL-BACKEND-2026-10-02`,
  `T-GPU-FLOAT-SSIM-FRAME-SUM-ORDER-2026-10-02`.
