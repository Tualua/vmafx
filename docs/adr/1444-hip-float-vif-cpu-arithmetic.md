<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1444: `float_vif_hip` runs the arithmetic of the CUDA twin from one shared header and returns the CPU's scores bit for bit

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `hip`, `cuda`, `gpu-parity`, `numerics`, `float-vif`, `testing`, `ci`, `rc3`, `fork-local`

## Context

[ADR-1412](1412-cuda-float-vif-cpu-arithmetic.md) found four properties of
the CPU `float_vif` extractor that a twin has to copy to return its bits (the
taps `vif_get_filter()` computes, the polynomial `log2f_approx()`,
`vif_sigma_nsq` as a `double`, and one fp32 sum per row and then over the
rows), and made `float_vif_cuda` copy them.
[ADR-1422](1422-sycl-float-vif-cpu-arithmetic.md) did the same for the SYCL
twin without an fp64 type. `float_vif_hip` still had all four differences
(`T-GPU-FLOAT-VIF-CPU-ARITHMETIC-2026-10-01`). Measured on a gfx1036
(ROCm 7.2.4) at `--precision max` against `--backend cpu` on `origin/master`
80c5a0332, frames whose score equals the CPU's on scale 0 / 1 / 2 / 3:

| Fixture | Frames | Identical | Max abs diff |
|---|---|---|---|
| Netflix 576x324, 8 bit | 48 | 0 / 0 / 0 / 0 | 3.8e-5 |
| Checkerboard 1 px, 1920x1080 | 3 | 0 / 0 / 0 / 0 | 1.05e-6 |
| Checkerboard 10 px, 1920x1080 | 3 | 1 / 3 / 3 / 3 | 1.1e-12 |
| Netflix 576x324, 10 bit | 3 | 0 / 0 / 0 / 0 | 1.07e-5 |
| Sparks 480x270, 10 bit | 5 | 0 / 0 / 0 / 0 | 3.4e-6 |
| BBB 3840x2160 | 48 | 0 / 0 / 0 / 0 | 7.0e-6 |
| Eight stress fixtures (12 and 16 bit, 4:2:2, noise, bright 16 bit) | 68 | 0 / 0 / 0 / 0 | 1.06e-4 |

10 of 440 scores of the first six fixtures were the CPU's. On a bright 16-bit
1920x1080 pair `vif_scale1` was 1.06e-4 from the CPU, which is above the 5e-5
the parity gate allows the twin, so this was a defect and not a rounding
residue.

The old kernel had a second defect, found when the new parity test ran
against it: it reflected an out-of-plane tile index once and did not clamp
the result. At scale 3 a 16x16 block of a plane narrower or shorter than
nine samples reflects index 16 to a negative index, so on a frame smaller
than 72 pixels in either dimension the kernel read in front of its buffer.
On the gfx1036 that is not a silent read: `--feature float_vif_hip` on a
64x64, 56x56 or 40x40 frame ended with `Memory access fault by GPU node-1`
on every one of three runs each.

A HIP device differs from a CUDA device in two ways that matter here. hipcc
has no `__fmul_rn()` family that rounds independently of the contraction
setting: its spellings are the plain operators
([ADR-1407](1407-hip-strict-fp-every-kernel.md)), and every HIP kernel is
built with `-ffp-contract=off` and correctly rounded fp32 division instead.
And whether the device's fp64 `+` and `/` round as the host's do had not
been measured.

## Decision

We will make `float_vif_hip` return the CPU extractor's values bit for bit by
running the arithmetic `float_vif_cuda` runs, from one header.

**One header.** The arithmetic and the kernel argument blocks of
`core/src/feature/cuda/float_vif/float_vif_device.h` move to the
backend-neutral `core/src/feature/float_vif_gpu_common.h`. Every operation
that rounds goes through a macro (`FVIF_FMUL`, `FVIF_FADD`, `FVIF_FSUB`,
`FVIF_FDIV`, `FVIF_DADD`, `FVIF_DDIV`) whose default is the plain operator. The
CUDA header keeps what is specific to CUDA: it maps the macros to the
`__fmul_rn()` family for device code, includes the shared header and names the
argument blocks as the CUDA sources do. Nothing in the arithmetic changed.

**The HIP twin uses the defaults.** `float_vif_score.hip` is the three
kernels of `float_vif_score.cu` (decimate, per-pixel terms, one thread per
row for the row sums) with the shared header's plain operators, which round
once under the strict FP list. The gfx1036's fp64 addition and division were
measured against the host over 33.5 million operand pairs spanning 2^-40 to
2^40 and six values of `vif_sigma_nsq`: the sum, the quotient and both
`(float)(1.0 + quotient)` results were the host's bits for every pair. The
host takes the taps from `vif_get_filter()`, hands each launch its scale's
taps and `vif_sigma_nsq` as a `double` in one argument block by value, and
adds the per-row sums with `fvif_sum_rows()`.

**Tile indices are clamped.** The tile loader passes every reflected index
through `vmaf_hip_tile_index()` (`hip_tile_index.h`), so a load that no output
consumes stays inside the plane.

**The CPU's option table.** The twin gains `vif_scale1_min_val`,
`vif_scale2_min_val` and `vif_scale3_min_val` (default 0, as on the CPU) and
applies them through the shared emitter.

**Gate.** `scripts/ci/exact_twins.d/float_vif.hip` declares the twin exact:
the CPU and HIP cells are compared with tolerance 0 at `--precision max`.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Move the arithmetic to a shared header and give HIP the plain operators (this ADR) | One implementation of the statistic for both twins; the device-free test that compares it with `vif_tools.c` covers HIP as it is, because HIP compiles the operators the host compiles | The CUDA header becomes a wrapper; a CUDA file is touched by a HIP change | Chosen |
| A HIP copy of `float_vif_device.h` | No CUDA file touched | Two copies of `vif_pixel_statistic_s()` to keep in step with the CPU | HISS-19 |
| Include the CUDA header from the HIP kernel with `DEVICE_CODE` | No new file | `__fmul_rn()` and `__dadd_rn()` do not exist for a HIP device; the header would grow a second device branch under a CUDA path | A backend-neutral file is the honest place |
| The SYCL twin's fp64-free statistic (`sycl_float_vif_math.h`) | No fp64 on the device | SYCL C++ in its own namespace, built for devices without an fp64 type; the gfx1036 has one and its fp64 `+` and `/` measured correct | Not needed here; a tuning candidate (fp64 costs 4.4 ms of a 1080p frame) |
| Update the tap table and keep the rest | Smallest change; removes 3.8e-5 | Leaves the other three differences, up to 1.06e-4 on bright 16-bit input | Not exact |
| Keep per-block partial sums and add them on the host in the CPU's order | No 66 MB term plane | The CPU's sum is one running fp32 accumulation per row; a block's partial is a different number however the host adds it | Not exact |

## Consequences

- **Positive**: measured on a gfx1036 at `--precision max`, every score of
  every frame equals `--backend cpu`: 440 of 440 on the six typical fixtures
  (10 before) and 272 of 272 on the eight stress fixtures (0 before, up to
  1.06e-4 off). With `debug=true` (fifteen outputs: the frame ratio and every
  per-scale sum), `vif_enhn_gain_limit=1.0` with `vif_sigma_nsq=1.5`,
  `vif_sigma_nsq=4.7`, `vif_skip_scale0` and the per-scale floors, the
  outputs are identical too on 62 frames from 576x324 at 8 to 16 bits to
  1920x1080 (1922 values).
- **Positive**: frames from 16x16 up to 71 pixels in either dimension run.
  They ended in a GPU memory fault before
  (`T-HIP-FLOAT-VIF-SMALL-FRAME-GPU-FAULT-2026-10-02`).
- **Positive**: `vif_pixel_statistic_s()` exists once for the CUDA and HIP
  twins.
- **Negative**: a frame takes longer. Steady state inside one process, 11
  interleaved pairs of runs, host load average 3 to 13: 20.7 ms before and
  26.0 ms after at 1920x1080 (+26 %), 86.0 and 147.1 ms at 3840x2160 (+71 %).
  Where it goes, measured by taking one property out of the new twin at a
  time (5 pairs each): the fp64 quotients and sums cost 4.4 ms at 1920x1080
  and 8.3 ms at 3840x2160; the term plane (two floats per pixel, 66 MB at
  3840x2160, written by the compute kernel and read by the row sums) costs
  1.1 ms and 37.8 ms; adding the rows in one thread each costs 0.6 ms and
  nothing measurable. Storing the terms row by row instead of column by
  column is slower (171.9 ms). `T-HIP-FLOAT-VIF-EXACT-THROUGHPUT-2026-10-02`.
- **Negative**: 66 MB more device memory at 3840x2160.
- **Negative**: stored `float_vif_hip` outputs change by up to 3.8e-5 on
  typical content and 1.06e-4 on bright 16-bit content.
- **Neutral / follow-ups**:
  - `float_vif_metal` is the last twin with the old tap table
    (`T-GPU-FLOAT-VIF-CPU-ARITHMETIC-2026-10-01` stays open for it).
  - The CUDA twin was rebuilt with the header split and its device and
    device-free tests re-run.
  - Guards: `test_hip_float_vif_parity` and `_large` (seven cases, three
    frames each, `==` on every output; the first frame it runs is 64x64),
    `test_hip_float_vif_exact_contract.py` (nine planted regressions, no
    device), `test_float_vif_device_math` (the shared header against
    `vif_tools.c`) and `test_cuda_float_vif_exact_contract.py`, which now
    reads the shared header.

## References

- `req` (coordinator brief for the HIP lane, 2026-10-02): "`float_vif_hip`: port the CUDA arithmetic (`float_vif_device.h`, ADR-1412; the SYCL twin did the same without fp64, ADR-1422). It is above its own 5e-5 gate tolerance on bright 16-bit today, so this one is a defect, not polish. Reuse the shared headers; a helper that must move to a backend-neutral place moves in this PR."
- `req` (same brief): "New cost rule from here: above 20 % = say where the time goes and open the tuning row in the same PR, then land; stop and report only above 3x."
- [ADR-1412](1412-cuda-float-vif-cpu-arithmetic.md),
  [ADR-1422](1422-sycl-float-vif-cpu-arithmetic.md),
  [ADR-1407](1407-hip-strict-fp-every-kernel.md),
  [ADR-1421](1421-rc3-rc8-candidate-map.md),
  [ADR-1428](1428-exact-twins-fragments.md),
  [ADR-1217](1217-gpu-float-vif-options-reach-kernel.md),
  [ADR-0214](0214-gpu-parity-ci-gate.md),
  [ADR-0024](0024-netflix-golden-preserved.md).
- `docs/state.md`: `T-GPU-FLOAT-VIF-CPU-ARITHMETIC-2026-10-01`,
  `T-HIP-FLOAT-VIF-SMALL-FRAME-GPU-FAULT-2026-10-02`,
  `T-HIP-FLOAT-VIF-EXACT-THROUGHPUT-2026-10-02`.
