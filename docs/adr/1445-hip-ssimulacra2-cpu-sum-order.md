<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1445: `ssimulacra2_hip` evaluates the CPU's fp64 terms and returns the sums of the CPU's loops

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `hip`, `gpu-parity`, `numerics`, `ssimulacra2`, `testing`, `ci`, `rc3`, `fork-local`

## Context

Since [ADR-1390](1390-hip-ssimulacra2-device-resident.md) `ssimulacra2_hip`
runs the whole frame on the device and reproduces the CPU extractor's planes
bit for bit (colour conversion, XYB, blurs, downsample). Two things in the
last stage differed. `ssimulacra2.c::ssim_map()` and `::edge_diff_map()`
evaluate six terms per pixel and channel in `double` and add each into one
`double`, pixel after pixel. The twin, a port of the SYCL twin for devices
without an fp64 type, evaluated the terms as pairs of floats (relative error
about 2^-44) and added them in a fixed tree.

Measured on a gfx1036 (ROCm 7.2.4) at `--precision max` against
`--backend cpu` on `origin/master` 80c5a0332, no frame of 178 equalled the
CPU (`T-HIP-SSIMULACRA2-NOT-CPU-BITS-2026-10-01`):

| Fixture | Frames | Identical | Max abs diff |
|---|---|---|---|
| Netflix 576x324, 8 bit | 48 | 0 | 1.1e-12 |
| Checkerboard 1 px, 1920x1080 | 3 | 0 | 2.6e-13 |
| Checkerboard 10 px, 1920x1080 | 3 | 0 | 7.6e-11 |
| Netflix 576x324, 10 bit | 3 | 0 | 7.5e-13 |
| Sparks 480x270, 10 bit | 5 | 0 | 3.1e-12 |
| BBB 3840x2160 | 48 | 0 | 5.8e-13 |
| Eight stress fixtures (12 and 16 bit, 4:2:2, noise, bright 16 bit) | 68 | 0 | 4.2e-12 |

[ADR-1433](1433-cuda-ssimulacra2-cpu-sum-order.md) solved the sum for the
CUDA twin, which already evaluated the terms in fp64:
`core/src/feature/ordered_sum.h` returns the bits of a sequential loop from
integer increments per binade that the device forms in parallel. An AMD device
has an fp64 type as well, but whether its fp64 operations return the host's
bits for these expressions had not been measured.

## Decision

We will make `ssimulacra2_hip` return the CPU extractor's score bit for bit,
by the design of ADR-1433.

**The CPU's terms.** `ss2h_terms()` is `ssimulacra2_device.cu::ss2c_terms()`:
the fp32 part of `ssim_map()` verbatim, then
`1.0 - (double)num_m * (double)num_s / (double)denom_s` and
`(1.0 + ed2) / (1.0 + ed1) - 1.0` in `double`, the edge difference split by
the shared `vmaf_ss2_split_edge_difference()`. The pair arithmetic is gone
from the module. A probe on the gfx1036 evaluated the six terms for 16.8
million pixels (variances and differences from 1e-5 to 1 of the signal) on
the device and on the host: every term had the host's bits. The module is
built without contraction, as before.

**The CPU's sums.** The four kernels of the CUDA twin, for HIP:
`ssimulacra2_chunk_sums` (each 1024-pixel chunk's six sums in a tree, as
advice), `ssimulacra2_chunk_plan` (the binade each chunk starts in),
`ssimulacra2_chunk_units` (each chunk's terms as integer increments of that
binade, composed in pixel order) and `ssimulacra2_ordered_totals` (one walk
per sum; a chunk whose plan does not hold at the exact sum is added term by
term). The arithmetic is the shared `ordered_sum.h`, unchanged. The readback
stays one 864-byte block, now 108 doubles instead of 108 float pairs, and
`collect()` reads the sums as they are.

**Gate.** `scripts/ci/exact_twins.d/ssimulacra2.hip` declares the twin exact:
the cell is compared with tolerance 0 instead of `5e-3`.

## Alternatives considered

Times are per 1920x1080 frame on the gfx1036; the old twin took 58.1 ms.

| Option | Result | Verdict |
|---|---|---|
| Keep the fp32 pairs and port only the ordered sum | Not exact: a pair is within 2^-44 of the CPU's double, not equal to it, and `ordered_sum.h` works on the bits of the terms | Rejected |
| fp64 terms, the tree kept | Not exact: on the CUDA twin, which had the CPU's terms, the order alone was up to 7.3e-11 | Rejected |
| **fp64 terms and the four kernels of ADR-1433, with its chunk geometry (256 lanes, 4 pixels each)** | Bit-identical on every measured frame; 167.0 ms | **Chosen** |
| The same with 64 lanes of 16 pixels per chunk | Bit-identical; 227 ms | Rejected: slower |
| The plan's tree sums from the old fp32 pairs, the increments from fp64 terms | Not built. The plan is advice, so the result would be the same; the tree-sum pass costs 36.9 ms in fp64 and the whole old combine stage cost about 12.5 ms | A tuning candidate (`T-HIP-SSIMULACRA2-EXACT-THROUGHPUT-2026-10-02`); it keeps two evaluations of the terms in two arithmetics |
| Read the terms back and add them on the host | 600 MB per 3840x2160 frame at scale 0 | Rejected (ADR-1433) |
| Share the four kernels between the `.cu` and the `.hip` file through one header | One copy of 230 lines | Not now: the two modules already each carry their own colour conversion, blur and downsample, and the CUDA source contract pins strings in the `.cu` file. A candidate for the deduplication phase (RC5) |

## Consequences

- **Positive**: measured on a gfx1036 at `--precision max`, the score of
  every frame equals `--backend cpu`: 178 of 178 on the fourteen fixtures
  above (0 before), and 186 of 186 with `yuv_matrix` 1, 2 and 3 on 62 frames
  of six of them. `scripts/dev/speed_gpu_parity.py --backend hip --feature
  ssimulacra2` reports 48/48 and 50/50 at its default bound of 0.
- **Positive**: the colour conversion, the cube root and the transfer function
  of the HIP module are confirmed equal to the CPU's by the same measurement;
  `T-HIP-SSIMULACRA2-NOT-CPU-BITS-2026-10-01` had left that open.
- **Negative**: the twin takes 2.8 to 2.9 times as long. Steady state inside one
  process, 11 interleaved pairs of runs, host load average 7 to 8: 58.1 ms
  before and 167.0 ms after at 1920x1080, 233.7 and 662.4 ms at 3840x2160.
  The CPU extractor takes 124 ms per 3840x2160 frame on sixteen threads, so on
  this integrated GPU the twin is the slower path, more so than before. Where
  it goes at 1920x1080, from launching one kernel twice per scale (three
  pairs each): `ssimulacra2_chunk_sums` 36.9 ms, `ssimulacra2_chunk_plan`
  1.9 ms, `ssimulacra2_chunk_units` 74.1 ms, `ssimulacra2_ordered_totals`
  8.5 ms, 121 ms together against about 12.5 ms for the tree they replace
  (at 3840x2160, two to three pairs each: about 141, 7, 314 and 15 ms).
  The fp64 terms are evaluated twice per scale, and the increments and their
  ordered composition through LDS cost as much again as the terms.
  `T-HIP-SSIMULACRA2-EXACT-THROUGHPUT-2026-10-02`.
- **Negative**: stored `ssimulacra2_hip` scores change by up to 7.6e-11.
- **Neutral / follow-ups**:
  - The twin holds 3.8 MB more device memory at 3840x2160 and launches 48
    kernels per frame instead of 36.
  - `ssimulacra2_sycl` keeps the pairs and the tree: its devices have no
    fp64 type (`T-GPU-SSIMULACRA2-SUM-ORDER-2026-10-01` stays open for it).
  - The arithmetic needs terms that are non-negative or NaN (ADR-1433); a
    change that lets a term go negative must not reach `ordered_sum.h`.
  - Guards: `test_hip_ssimulacra2_parity` and `_large` (seven cases, three
    frames each, `==`; 18 of the 21 frames differ on the old twin),
    `test_hip_ssimulacra2_exact_contract.py` (eleven planted regressions, no
    device) and `test_ordered_sum` (the shared arithmetic, on the host).

## References

- `req` (coordinator brief for the HIP lane, 2026-10-02): "`ssimulacra2_hip`: port ADR-1433 (`core/src/feature/ordered_sum.h`, per-binade sums)."
- `req` (same brief): "New cost rule from here: above 20 % = say where the time goes and open the tuning row in the same PR, then land; stop and report only above 3x."
- [ADR-1433](1433-cuda-ssimulacra2-cpu-sum-order.md),
  [ADR-1390](1390-hip-ssimulacra2-device-resident.md),
  [ADR-1363](1363-sycl-ssimulacra2-msssim-device-resident.md),
  [ADR-1407](1407-hip-strict-fp-every-kernel.md),
  [ADR-1421](1421-rc3-rc8-candidate-map.md),
  [ADR-1428](1428-exact-twins-fragments.md),
  [ADR-0214](0214-gpu-parity-ci-gate.md).
- [Research-1433](../research/1433-cuda-ssimulacra2-cpu-sum-order.md).
- `docs/state.md`: `T-HIP-SSIMULACRA2-NOT-CPU-BITS-2026-10-01`,
  `T-GPU-SSIMULACRA2-SUM-ORDER-2026-10-01`,
  `T-HIP-SSIMULACRA2-EXACT-THROUGHPUT-2026-10-02`.
