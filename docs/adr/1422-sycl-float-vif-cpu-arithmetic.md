<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1422: `float_vif_sycl` computes the CPU's arithmetic without an fp64 type and returns its scores bit for bit

- **Status**: Accepted
- **Date**: 2026-10-01
- **Deciders**: lusoris
- **Tags**: `sycl`, `gpu-parity`, `numerics`, `float-vif`, `testing`, `ci`, `rc3`, `fork-local`

## Context

[ADR-1412](1412-cuda-float-vif-cpu-arithmetic.md) found four properties of
the CPU `float_vif` extractor that a twin has to copy to return its bits, made
`float_vif_cuda` copy them, and left the SYCL, HIP and Metal twins as
`T-GPU-FLOAT-VIF-CPU-ARITHMETIC-2026-10-01`. `float_vif_sycl` had all four
differences. Measured on an Arc A380 at `--precision max` against
`--backend cpu`, by putting each one back alone into the corrected twin
([Research-1422](../research/1422-sycl-float-vif-fp64-free-statistic.md)),
largest difference over the four `vif_scale*` outputs:

| Difference of the old twin | Netflix 576x324 | Checkerboard 1 px | Checkerboard 10 px | BBB 3840x2160 |
|---|---:|---:|---:|---:|
| Gaussian taps from a table the CPU dropped in #758, not `vif_get_filter()` | 3.83e-5 | 5.1e-7 | 1.9e-13 | 7.5e-6 |
| Device `log2`, where the CPU's `log2f` is the polynomial `log2f_approx()` | 2.1e-7 | 0 | 0 | 1.2e-7 |
| `vif_sigma_nsq` as fp32, where `vif_pixel_statistic_s()` keeps it in fp64 | 1.3e-7 | 0 | 0 | 7.5e-8 |
| Accurate sums, where `vif_statistic_s()` adds a row into one `float` and the rows into another | 5.0e-7 | 1.0e-6 | 1.2e-12 | 5.9e-6 |
| All four (the old twin) | 3.81e-5 | 1.04e-6 | 1.1e-12 | 7.0e-6 |

A SYCL kernel has two constraints the CUDA kernel does not. It may not use the
fp64 type ([ADR-0220](0220-sycl-fp64-fallback.md)): one fp64 instruction blocks
the whole translation unit on Arc A-series. And on those GPUs under the xe
driver it may not use scratch memory
([ADR-1395](1395-sycl-kernels-no-scratch.md)). The third row of the table is
therefore not a type change here. The reference computes

```c
1.0f + (g * g * sigma1_sq) / (sv_sq + vif_sigma_nsq)
1.0f + (sigma1_sq) / (vif_sigma_nsq)
```

in fp64 and rounds each to fp32 once, when it is passed to `log2f()`. The twin
needs that fp32 value on every pixel without an fp64 operation.

## Decision

We will make `float_vif_sycl` return the CPU extractor's values bit for bit,
by the design of ADR-1412, with the two fp64 expressions evaluated in fp32
pairs and, next to a rounding boundary, in 64-bit integers.

**Taps from the CPU's routine.** `init_vif_taps()` calls `vif_get_filter()`
for `(float)vif_kernelscale`, as `float_vif.c::init()` does, and every launch
hands its scale's taps to the kernel by value (`VifTaps`). No kernel holds a
tap literal. The kernels index the taps with the constants of their unrolled
loops only, so the array stays out of private memory.

**The reference's statistic, without fp64.**
`core/src/feature/sycl/sycl_float_vif_math.h` holds
`vif_pixel_statistic_s()` and `log2f_approx()` operation for operation
(`pixel_sigmas()`, `pixel_statistic()`, `log2_approx()`). The two fp64
expressions go through `one_plus_ratio()`:

- an exact fp32 pair (`sycl_exact_fp.h`: `ff_add`, `ff_div`) gives
  `1 + numerator / denominator` to about 2^-44, and its rounding to fp32 is
  the reference's value unless the pair lies within 2^-12 of an fp32 step of
  a rounding boundary (`near_rounding_boundary()`);
- for those samples, about one in 1650, the reference's own sequence
  (`fl64(sv_sq + vif_sigma_nsq)`, the fp64 quotient, the fp64 sum, the
  conversion to fp32) is replayed in 64-bit integers (`SoftDouble`:
  `soft_add`, `soft_div`, `soft_to_float`), so the result is the reference's
  by construction, double rounding included.

`vif_sigma_nsq` reaches the kernel as a pair, as an integer significand and
exponent, and as the smallest fp32 value not below it, which turns the
reference's fp64 comparison `sigma1_sq < vif_sigma_nsq` into an fp32 one with
the same outcome. The host builds these (`make_noise_variance()`).

**Three kernels per scale.** The filter kernel stores each pixel's
`sigma1_sq`, `sigma2_sq` and `sigma12`. A statistic kernel, one work-item per
pixel, replaces the first two by the numerator and denominator terms. A row
kernel, one work-item per row, adds a row's terms left to right into one fp32
accumulator per output (`vif_row_sums()`). The readback is two floats per row
per scale, in one copy, and `sum_vif_rows()` adds them top to bottom on the
host in fp32. The TU contains no group, sub-group or atomic reduction.

**No scratch memory.** The statistic is its own kernel because, inside the
filter kernel, its integer path spilled registers next to the 16x16 tile
(1440 to 1600 bytes at the default register file). `soft_add()` and
`noise_plus()` select scalars, not structs: a struct select stayed in private
memory (512 bytes). The statistic kernel requests sub-group size 16.

**The CPU's option table.** The twin gains `vif_scale1_min_val`,
`vif_scale2_min_val` and `vif_scale3_min_val` (default 0, as on the CPU) and
applies them through the shared emitter.

**Gate.** `float_vif` lists `sycl` next to `cuda` in `EXACT_TWINS`
(`scripts/ci/cross_backend_calibration.py`): the CPU, CUDA and SYCL cells are
compared with tolerance 0 at `--precision max`. HIP and Metal keep places=4.

## Alternatives considered

Times are per 3840x2160 frame of the `vmaf` tool on an Arc A380; the old twin
took 20.5 ms.

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Pairs, with the integer replay next to a boundary; statistic in its own kernel at sub-group size 16 (this ADR) | The CPU's bits on every sample by construction; fp64-free; scratch-free; 24.0 ms | 3.4 ms more than the old twin | Chosen |
| Pairs only, no replay | Simpler; no 64-bit integers | Wrong on about one quotient in 1e8: 85 of 8.4e9 random operands, most of them exact ties where the reference's two roundings and the pair's one disagree. A 3840x2160 frame has 22 million quotients | Not bit-identical |
| Integer replay on every pixel, no pairs | One path | A 56-step division per term on every pixel | The pair decides 1649 of 1650 samples |
| Statistic inside the filter kernel | One launch and three plane writes fewer | Spills at the default register file; with the large register file on every scale it is scratch-free and takes 29.4 ms | Slower |
| Statistic kernel at sub-group size 8 or 32 with the large register file | — | 29.4 ms and 25.3 ms; at size 32 with the default register file it spills 480 to 640 bytes | Size 16 at the default register file is the fastest scratch-free shape measured |
| Taps in device memory, read per tap | No array in the kernel arguments | 24.5 ms against 24.1 ms; one more allocation | By value is faster and simpler |
| Flag the samples next to a boundary and finish them on the host in real fp64 | No integer division on the device | A data-dependent readback per frame | The replay is self-contained |
| Update the tap table and keep the rest | Smallest change; removes 3.8e-5 | Leaves three differences of up to 5.9e-6, and a second copy of numbers the CPU derives at run time | The direction is bit for bit; HISS-19 |
| Make the CPU order-independent (`double` sums, libm `log2f`) | Twins could reduce freely | Moves the Netflix golden values | [ADR-0024](0024-netflix-golden-preserved.md) |

## Consequences

- **Positive**: measured on an Arc A380 (xe, Level Zero) at `--precision max`,
  every output of every frame equals `--backend cpu`: the Netflix 576x324 pair
  at 8 bits (48 frames) and at 10, 12 and 16 bits (3 frames each), both
  1920x1080 checkerboard pairs (3 frames each) and BBB 3840x2160 (200 frames),
  with `debug=true` (the frame ratio and the eight per-scale sums included).
  Non-default `vif_enhn_gain_limit`, `vif_sigma_nsq` (0, 1.5, 4.7),
  `vif_skip_scale0` and the new floors are identical too. The same holds
  against a GCC build of the CPU extractor and against the CPU extractor of
  the icx build itself; the gate reports 0 on all four fixtures. Before: no
  frame of the Netflix pair, largest difference 3.81e-5.
- **Negative**: 3.4 ms more per 3840x2160 frame on the Arc A380 through the
  `vmaf` tool: 20.54 ms before, 23.95 after (medians of 15 paired 100-frame
  runs, host load 12 to 22; the untouched `float_psnr_sycl` read 3.34 and
  3.33 in the same session). On the Netflix 576x324 pair: 0.73 ms before,
  0.93 after (15 paired 44-frame runs). Launching a kernel twice per scale
  adds 10.0 ms for the filter and 0.8 ms for the row sums, so the filter is
  the larger part of the frame. The CPU extractor takes 68 ms per frame on
  16 threads. `T-SYCL-FLOAT-VIF-EXACT-THROUGHPUT-2026-10-01`.
- **Negative**: 100 MB more device memory at 3840x2160 (three floats per pixel
  of scale 0), in place of the per-group partial sums.
- **Negative**: stored `float_vif_sycl` outputs change by up to 3.8e-5.
- **Neutral / follow-ups**:
  - The contract mirrors three CPU properties. If any changes, the twin
    changes in the same PR: `vif_get_filter()` (the taps),
    `VIF_OPT_FAST_LOG2` and `log2f_approx()` (the polynomial), and
    `vif_pixel_statistic_s()` / `vif_statistic_s()` (types and order).
    `test_sycl_float_vif_math` compares the header with `vif_statistic_s()`
    on the host and in a kernel on the device and fails when they diverge;
    `test_sycl_float_vif_exact_contract.py` pins the design at source level;
    `test_sycl_float_vif_parity` compares the scores.
  - `core/test/float_vif_twin_parity.h` holds the fixtures, the cases and the
    exact comparison for any backend's twin; `test_sycl_float_vif_parity` is
    its first user. The CUDA test carries the same cases in its own file.
  - The twin uses no scratch memory (`test_sycl_kernel_scratch`, 114 kernels
    audited on the A380, the ratchet list unchanged).
  - `float_vif_hip` and `float_vif_metal` still carry the tap table and call
    their device `log2`: `T-GPU-FLOAT-VIF-CPU-ARITHMETIC-2026-10-01` stays
    open for them.

## References

- `req` (maintainer brief for the SYCL exactness lane, 2026-10-01): "results before speed; a twin reproduces the CPU bit for bit, tuning comes afterwards".
- [ADR-1412](1412-cuda-float-vif-cpu-arithmetic.md) (the decision this applies
  to SYCL), [ADR-1397](1397-psnr-hvs-twins-cpu-float-sum.md) (the exact cell),
  [ADR-1367](1367-sycl-strict-fp-every-feature-tu.md) (contraction off),
  [ADR-1395](1395-sycl-kernels-no-scratch.md),
  [ADR-0220](0220-sycl-fp64-fallback.md),
  [ADR-1401](1401-psnr-hvs-sycl-hip-exact-twins.md) (the exact fp32 pairs),
  [ADR-0416](0416-vif-upstream-onthefly-filter-sync.md),
  [ADR-1217](1217-gpu-float-vif-options-reach-kernel.md),
  [ADR-0214](0214-gpu-parity-ci-gate.md),
  [ADR-0024](0024-netflix-golden-preserved.md).
- [Research-1422](../research/1422-sycl-float-vif-fp64-free-statistic.md).
- `docs/state.md`: `T-GPU-FLOAT-VIF-CPU-ARITHMETIC-2026-10-01` (SYCL part
  closed by this decision).
