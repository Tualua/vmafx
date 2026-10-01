<!-- markdownlint-disable MD013 MD060 -->
# Research-1422: float_vif on SYCL without fp64 — the four differences on an Arc A380, a pair that is almost enough, and the kernel shapes measured

- **Status**: Active
- **Workstream**: [ADR-1422](../adr/1422-sycl-float-vif-cpu-arithmetic.md), [ADR-1412](../adr/1412-cuda-float-vif-cpu-arithmetic.md), [ADR-0220](../adr/0220-sycl-fp64-fallback.md), [ADR-1395](../adr/1395-sycl-kernels-no-scratch.md)
- **Last updated**: 2026-10-01

## Question

[Research-1412](1412-cuda-float-vif-cpu-arithmetic.md) derived the CPU
`float_vif` arithmetic from source and listed four properties a twin has to
copy. `float_vif_sycl` had none of them. How large is each on the SYCL twin,
how can the two fp64 expressions of `vif_pixel_statistic_s()` be evaluated on
a device without an fp64 type so that the fp32 result is the reference's on
every sample, and which kernel shape does that without scratch memory at the
lowest cost?

## Sources

- CPU: `core/src/feature/vif_tools.c` (`vif_get_filter()`,
  `vif_pixel_statistic_s()`, `vif_statistic_s()`, `log2f_approx()`),
  `core/src/feature/float_vif.c`, `core/src/feature/vif_options.h`.
- SYCL: `core/src/feature/sycl/float_vif_sycl.cpp` at master `5d59e9485`
  (before; the binary the timings use was built at `1e52ad612`, where the
  file is the same) and on `fix/sycl-float-vif-cpu-arithmetic` (after),
  `core/src/feature/sycl/sycl_exact_fp.h`,
  `core/src/feature/sycl/sycl_float_vif_math.h` (new).
- Host: Arc A380 (xe driver, Level Zero, compute runtime 26.35.39758), icpx
  2026.0.0, gcc 16.2.1, Linux 7.2.8, Ryzen 9 9950X3D. `meson setup build-sycl
  core -Denable_sycl=true -Denable_cuda=false -Dsycl_icpx_aot_targets=
  --buildtype=release -Db_lto=false`; the CPU reference is a GCC build.
- Fixtures, 4:2:0, `--precision max`: the Netflix pair
  `src01_hrc00/01_576x324` at 8 bits (48 frames) and its 10-, 12- and 16-bit
  versions (3 frames each), the checkerboard pairs
  `checkerboard_1920_1080_10_3_0_0` against `_1_0` and `_10_0` (3 frames
  each), and BBB 3840x2160.

## Findings

### 1. The four differences, one at a time

Each row puts one property of the old twin back into the corrected one and
leaves the other three corrected. Largest absolute difference against
`--backend cpu` over `vif_scale0..3`; BBB over its first 20 frames.

| Property put back | Netflix 576x324 | Checkerboard 1 px | Checkerboard 10 px | BBB 3840x2160 |
|---|---:|---:|---:|---:|
| none (the corrected twin) | 0 | 0 | 0 | 0 |
| the old tap table | 3.83e-5 | 5.1e-7 | 1.9e-13 | 7.5e-6 |
| `sycl::log2` | 2.1e-7 | 0 | 0 | 1.2e-7 |
| `vif_sigma_nsq` in fp32 | 1.3e-7 | 0 | 0 | 7.5e-8 |
| row sums added in fp64 on the host | 4.6e-7 | 6.5e-7 | 5.2e-13 | 1.8e-6 |
| compensated row sums and an fp64 sum of the rows | 5.0e-7 | 1.0e-6 | 1.2e-12 | 5.9e-6 |
| the old twin | 3.81e-5 | 1.04e-6 | 1.1e-12 | 7.0e-6 |

The taps dominate on real content, as on CUDA. The last two rows are the
order of the additions: the CPU's running fp32 sums carry a rounding error of
their own, and a twin that adds more accurately is further from the CPU, most
at 3840x2160. On the checkerboards the device `log2` and the fp32 variance
change no output.

### 2. The fp64 expressions as fp32 pairs

`one_plus_ratio_pair()` forms `1 + n / d` with `ff_div()` and `ff_add()` of
`sycl_exact_fp.h`: two correctly rounded quotient digits and an exact
two-sum, relative error about 2^-44. The reference's value is
`fl32(fl64(1 + fl64(n / fl64(d))))`. Both are within about 2^-43 of the true
value, so their fp32 roundings can differ only when the true value lies
within that distance of a point where the rounding changes.

A host program evaluated both forms and the true fp64 expression on random
operands over the ranges the statistic produces (gains to 100, variances from
1e-9 to 16000, `vif_sigma_nsq` of 0.3, 1.5, 2, 4.7 and 5):

| | Count |
|---|---:|
| quotients evaluated | 8.4e9 |
| pair alone differs from fp64 | 85 |
| integer replay differs from fp64 | 0 |
| selected value (pair, or replay next to a boundary) differs from fp64 | 0 |
| samples that take the replay | 1 in 1640 |

Most of the 85 are exact ties: the true sum lies exactly halfway between two
fp32 values. The reference rounds twice (to fp64, then to fp32), the pair
once, and a tie resolves differently. `0x1.333334p-26 / 0.3` is the shortest
example: the reference gives `0x1.000002p+0`, the pair `1.0`. Twelve of them
are the witness table of `test_sycl_float_vif_math`.

`near_rounding_boundary()` sends a sample to the replay when the pair's low
word is within 2^-12 of an fp32 step of half a step. The pair's error is
about 2^-21 of a step, so the margin is a factor of 500.

### 3. The integer replay

`SoftDouble` is a positive fp64 value as a 53-bit significand and an
exponent. `soft_add()` aligns with a sticky bit and rounds to nearest even;
`soft_div()` is a 56-step restoring division whose remainder is the sticky
bit; `soft_to_float()` rounds the fp64 result to fp32. Chained in the
reference's order they reproduce its double rounding. On the device this
costs nothing measurable: a sub-group runs the loop only when one of its
lanes needs it.

### 4. Kernel shapes

Per 3840x2160 frame of the `vmaf` tool, medians of 7 paired 50-frame runs,
host load 10 to 22. The old twin: 20.5 ms.

| Shape | Scratch memory | ms / frame |
|---|---|---:|
| statistic inside the filter kernel, default register file on scales 1 to 3 | private 512 B, spill 1440 to 1600 B | wrong values on xe |
| statistic inside the filter kernel, large register file, scalar selects | none | 29.4 |
| own kernel, sub-group 8, large register file | none | 29.4 |
| own kernel, sub-group 16, large register file | none | 26.3 |
| own kernel, sub-group 32, large register file | none | 25.3 |
| own kernel, sub-group 32, default register file | spill 480 to 640 B | not usable |
| own kernel, sub-group 16, default register file, taps in device memory | none | 24.5 |
| own kernel, sub-group 16, default register file, taps by value (chosen) | none | 24.1 |

The first row is what a direct port produced: the kernel compiled, and on the
A380 under xe it returned a denominator of -1.6e29 for scale 0. The private
512 bytes came from selecting between two `SoftDouble` structs; selecting
their fields removed it.

Launching a kernel twice per scale, which leaves the scores unchanged, adds
10.0 ms for the filter kernel and 0.8 ms for the row sums. The filter is
unchanged in structure, so most of a frame is still the filter and the
17x17-tap decimation of scale 1.

### 5. After

Identical outputs against `--backend cpu` at `--precision max`: Netflix
576x324 48 of 48 frames on each of the four scales (before 0 of 48), 10, 12
and 16 bits 3 of 3, both checkerboards 3 of 3, BBB 3840x2160 200 of 200, and
with `debug=true` all 15 outputs on each of those. Options
`vif_enhn_gain_limit=1.0`, `vif_sigma_nsq` of 0, 1.5 and 4.7,
`vif_skip_scale0=true` and the three floors at 0.99: identical on the Netflix
pair and the 10 px checkerboard. The gate with the icx binary on both sides:
0 on the Netflix pair, both checkerboards and all 200 BBB frames.

Final timing, 15 paired runs: 20.54 to 23.95 ms per 3840x2160 frame (100
frames), 0.73 to 0.93 ms per 576x324 frame (44 frames); `float_psnr_sycl` as
a control read 3.34 and 3.33 ms.

## Open

- The filter kernel now writes three planes and the statistic kernel reads
  them back; a shape that keeps the statistic in registers next to the tile
  without spilling would save that traffic
  (`T-SYCL-FLOAT-VIF-EXACT-THROUGHPUT-2026-10-01`).
- Not measured on an Arc B580 or an integrated GPU.

## Reproduce

```bash
ONEAPI_DEVICE_SELECTOR=level_zero:0 python3 scripts/dev/speed_gpu_parity.py \
    --backend sycl --vmaf "$PWD/build-sycl/tools/vmaf" --feature float_vif
meson test -C build-sycl test_sycl_float_vif_math test_sycl_float_vif_parity \
    test_sycl_float_vif_exact_contract test_sycl_kernel_scratch
```
