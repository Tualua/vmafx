<!-- markdownlint-disable MD013 MD060 -->
# Research-1434: float_adm on SYCL without fp64 — the differences on an Arc A380 one at a time, and three fp64 expressions as fp32 pairs

- **Status**: Active
- **Workstream**: [ADR-1434](../adr/1434-sycl-float-adm-cpu-arithmetic.md), [ADR-1420](../adr/1420-cuda-float-adm-cpu-arithmetic.md), [ADR-0220](../adr/0220-sycl-fp64-fallback.md), [ADR-1395](../adr/1395-sycl-kernels-no-scratch.md)
- **Last updated**: 2026-10-02

## Question

[Research-1420](1420-cuda-float-adm-cpu-arithmetic.md) read the CPU
`float_adm` arithmetic from source and listed what a twin has to copy.
`float_adm_sycl` copied little of it and was up to 1.7e-5 from the CPU. How
much does each difference contribute on the SYCL twin, and how can the three
expressions `adm_tools.c` evaluates in fp64 be evaluated on a device without
an fp64 type so that the fp32 result is the reference's on every sample?

## Sources

- CPU: `core/src/feature/adm_tools.c` (`DIVS()`, `adm_angle_flag_s()`,
  `adm_decouple_band_s()`, `adm_csf_s()`, `adm_cm_thresh3x3_s()`,
  `adm_csf_den_scale_s()`, `adm_cm_s()`, `adm_pool_bands_s()`),
  `core/src/feature/adm.c::compute_adm()`, `core/src/feature/float_adm.c`.
- SYCL: `core/src/feature/sycl/float_adm_sycl.cpp` at master `4358f1b8a`
  (before) and on `fix/sycl-float-adm-cpu-arithmetic-exact` (after),
  `core/src/feature/sycl/sycl_float_adm_math.h` (new),
  `core/src/feature/sycl/sycl_exact_fp.h`,
  `core/src/feature/sycl/sycl_soft_double.h`.
- Host: Arc A380 (xe driver, Level Zero, compute runtime 26.35.39758), icpx
  2026.0.0, gcc 16.2.1, Linux 7.2.8, Ryzen 9 9950X3D. `meson setup build-sycl
  core -Denable_sycl=true -Denable_cuda=false -Dsycl_icpx_aot_targets=
  --buildtype=release -Db_lto=false`; the CPU reference is a GCC build unless
  a line says otherwise.
- Fixtures, 4:2:0, `--precision max`: the Netflix pair
  `src01_hrc00/01_576x324` at 8 bits (48 frames) and its 10-, 12- and 16-bit
  and 4:2:2 10-bit versions (3 frames each), the checkerboard pairs
  `checkerboard_1920_1080_10_3_0_0` against `_1_0` and `_10_0` (3 frames
  each), and BBB 3840x2160.

## Findings

### 1. The differences, one at a time

Each row puts one property back into the corrected twin and leaves the
others corrected. Largest absolute difference against `--backend cpu` over
`adm2`, `adm3`, `aim` and the four scale scores, and the number of identical
outputs; BBB over its first 20 frames. The reference is the CPU extractor
with [ADR-1442](../adr/1442-float-adm-reference-divides.md), whose decouple
divides.

| Property put back | Netflix 576x324 (336) | Checkerboard 1 px (21) | Checkerboard 10 px (21) | BBB 3840x2160 (140) |
|---|---:|---:|---:|---:|
| none (the corrected twin) | 0 (336) | 0 (21) | 0 (21) | 0 (140) |
| fp32 `1/30` and `1/15` | 1.2e-9 (306) | 0 (21) | 0 (21) | 2.4e-9 (130) |
| angle threshold as `cos^2 * (o^2 * t^2)` | 2.43e-6 (333) | 0 (21) | 0 (21) | 1.28e-5 (122) |
| centre tap of the threshold last | 1.2e-9 (286) | 1.4e-9 (19) | 0 (21) | 2.4e-9 (128) |
| rows added in fp64 on the host | 1.93e-7 (139) | 3.5e-7 (2) | 5.1e-8 (2) | 1.8e-7 (52) |
| fp32 gain, `adm_enhn_gain_limit=1.2` | 0 (336) | 0 (21) | 0 (21) | 6.0e-10 (138) |
| the quotient as `n * (1 / d)` (not a property of the old twin) | 1.0e-7 (282) | 6.6e-8 (18) | 0 (21) | 8.0e-8 (122) |
| the old twin | 2.5e-6 (59) | 3.5e-7 (1) | 1.5e-7 (4) | 1.28e-5 (24) |

Over 200 BBB frames the old twin matches 301 of 1400 outputs and is up to
1.7e-5 off (`adm_scale3`).

The association of the angle threshold is the whole of the old twin's largest
error and changes the fewest outputs: the product is one fp32 step different
on a sample whose `(h, v)` vectors are one degree apart, the flag flips, and
that sample's restored signal changes from the reference coefficient to the
gain-limited one. The order of the additions changes the most outputs by the
least. At the default gain limit of 100 the fp32 gain is the reference's
(the fp64 product of two fp32 values is exact when the limit is an fp32
value), so that row uses 1.2.

The frame floor and the twin's own copy of the CSF weights change no output
on these fixtures at the default options. The floor does on near-flat
content with `adm_noise_weight=0`, where the twin reported `adm2 = 1` and the
CPU 0 (`T-GPU-FLOAT-ADM-FRAME-SUM-FLOOR-2026-10-01`).

### 2. Three fp64 expressions as fp32 pairs

```c
flt = FLOAT_ONE_BY_30 * fabsf(dst_val);     /* adm_csf_s() */
sum += FLOAT_ONE_BY_15 * fabsf(src_ptr[j]); /* adm_cm_thresh3x3_s() */
rst = MIN(rst * adm_enhn_gain_limit, t);    /* adm_decouple_band_s() */
```

`FLOAT_ONE_BY_30` is the `double` literal `0.0333333351`, not the fp32 value
of that name: the two differ by about 2^-30 of their value, enough to move
roughly one product in a hundred across an fp32 rounding boundary. A kernel
carries the literal as `hi + lo`, two fp32 values whose sum is the literal to
2^-48, and as its 53-bit significand.

*Product.* `two_prod(a, hi)` is exact (an fp32 product and its exact
residual, `sycl_exact_fp.h`), `a * lo` is rounded once, and the pair
`hi + lo` of the result is within about 2^-47 of the exact product, 2^-24 of
an fp32 step. The fp64 product the reference forms is within 2^-53 of it,
2^-30 of a step. The fp32 result is the pair's high word unless the pair
lies near a point where the rounding changes. `undecided()` reports a pair
whose low word is within 2^-18 of a step of half a step: 64 times the pair's
error. For those the kernel multiplies the 24-bit and 53-bit significands in
integers, rounds to 53 bits as fp64 does and then to fp32
(`times_constant_replayed()`).

*Sum.* The reference adds the fp64 product to the fp32 running sum in fp64
and rounds to fp32. `ff_add` forms the pair of `sum + product`; the same zone
decides between the pair and `add_scaled_replayed()`, which adds in a 53-bit
`SoftDouble` and rounds twice as the reference does.

*Gain.* For an fp32 limit the fp64 product has at most 48 significant bits
and is exact, so `(float)(rst * limit)` is the fp32 product, and since
rounding is monotonic the comparison of the rounded product with `t` selects
what the fp64 comparison selects whenever the two results differ. For any
other limit the product and the comparison are replayed.

Counted on the host against the same C expressions, 1.8e9 samples of every magnitude (uniform bit patterns,
band magnitudes, subnormals, values next to one, zeros) with six gain limits
and a quarter of the clamp's inputs at the product or one fp32 step either
side of it: no result wrong. By the width of the zone one evaluation in
131 072 is replayed when the low bits of the product are spread evenly; the
count on real content was not taken.

`sycl::mul_hi()` on 64-bit operands is not used: it returned wrong values in
a kernel on the A380 ([ADR-1432](../adr/1432-sycl-integer-vif-exact-gain.md)).

### 3. The reference's division on a device

Since [ADR-1442](../adr/1442-float-adm-reference-divides.md) `DIVS(n, d)` is
`n / d` on every host. The kernel writes the same quotient. Every SYCL
feature TU is compiled with `-foffload-fp32-prec-div`
([ADR-1367](../adr/1367-sycl-strict-fp-every-feature-tu.md)), under which
the device's fp32 division is correctly rounded; whether it is on this
device is checked per value, not assumed: `test_decouple_csf_device` runs
the decouple kernel over 64 random scales and compares every restored and
additive sample with `adm_decouple_s()` on the host, and the frame
comparisons below are zero on 4K content.

Upstream, and this fork until ADR-1442, multiplied by a reciprocal refined
from the processor's `RCPSS` estimate, whose bits the instruction set does
not specify. The first version of this change followed that: it read the
host's estimate from a probed 4096-entry table on the device and was
bit-identical to the CPU of the same machine. That path is gone with the
reference's.

### 4. Kernels and time

| | Before | After |
|---|---|---|
| Kernels per scale | DWT vertical, DWT horizontal, decouple + CSF, CSF of the restored signal, two masked reductions | DWT vertical, DWT horizontal, decouple + both CSF passes, terms, row sums |
| Reduction | per work-item over strided columns, per sub-group, per work-group; the host adds in fp64 | one work-item per row adds it left to right (sub-group size 8); the host adds the rows in fp32 |
| Device-to-host copies per frame | 4 | 1 |
| Extra device memory at 3840x2160 | | 48 MB (nine terms per sample of the reduced region) |
| Scratch memory | none | none (118 kernels audited) |

Through the `vmaf` tool on the A380, medians of 11 runs
(`(t(long) - t(short)) / frames`, host load 9 to 11):

| | Before | After |
|---|---:|---:|
| BBB 3840x2160, ms per frame | 15.12 | 12.33 |
| Netflix 576x324, ms per frame | 0.57 | 0.59 |
| control: `float_psnr` 3840x2160 | 3.28 | 3.27 |

The twin got faster at 3840x2160. It runs five kernels per scale where the
old one ran six and makes one device-to-host copy where the old one made
four; the stages were not timed separately. The first version of this
change, which read a reciprocal estimate from a table in device memory for
every decouple sample, took 15.8 ms there.

### 5. What is left

- `adm_p_norm` other than 1 or 3: the terms are `pow(x, p)` on the device and
  `powf(x, p)` on the host. 1.8e-7 at most at 2, 2.5 and 4.5.
- The CPU extractor of an icx build against that of a GCC build:
  `adm_pool_bands_s()` calls the host's `powf`. With the default options
  `aim` and `adm3` differ on 2 of 200 BBB frames (1.6e-9 and 8.0e-10); with
  a CSF weight override (`adm_f1s3=2.25:adm_f2s0=0.3`) one of 48 Netflix
  frames differs by up to 7.5e-8, and with another viewing distance
  (`adm_norm_view_dist=4.5:adm_ref_display_height=2160`) by 2.1e-10.
  The twin equals the CPU extractor of its own build in each case
  (`T-ICX-LIBIMF-HOST-MATH-2026-10-01`).

### 6. A header edit did not rebuild the kernels

The first run of the table in section 1 showed 0 in every row: meson compiles
each SYCL translation unit with a custom target that declared only its
source, so an edit to `sycl_float_adm_math.h` rebuilt nothing and every row
measured the corrected twin. The rows above were taken after touching the
source file. `T-SYCL-TU-HEADER-DEPS-UNTRACKED-2026-10-01` records it; the fix
(compiler depfiles for the SYCL targets) is a change of its own.

## Reproduce

```bash
python3 scripts/dev/speed_gpu_parity.py --backend sycl --feature float_adm \
    --vmaf "$PWD/build-sycl/tools/vmaf" --max-abs-diff 0
flock ~/.cache/vmafx-locks/sycl-a380.lock timeout 300 \
    build-sycl/test/test_sycl_float_adm_math
flock ~/.cache/vmafx-locks/sycl-a380.lock timeout 300 \
    build-sycl/test/test_sycl_float_adm_parity
```
