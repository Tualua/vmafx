<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1463: `float_ssim_sycl` forms the CPU's fp64 terms in 64-bit integers and adds them on the host in the CPU's raster order

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `sycl`, `gpu-parity`, `numerics`, `ssim`, `testing`, `rc3`, `fork-local`

## Context

`float_ssim_sycl` is a declared exact twin
([ADR-1451](1451-sycl-exact-twins-declared.md)): the parity gate compares
`float_ssim` and `float_ssim_lcs` against the CPU with tolerance 0. On every
frame of the sweep behind that declaration the twin returned the CPU's bits.

It was not the CPU's `float_ssim` on every input. The CUDA lane constructed a
64x64 pair (`T-GPU-FLOAT-SSIM-FRAME-SUM-ORDER-2026-10-02`) on which the CPU
scores -4.222829943500983e-07 (float bits `0xb4e2b622`) and the CUDA, HIP and
SYCL twins all score `0xb4e2b621`, one float step away.

`iqa/ssim_tools.c::iqa_ssim()` forms, for each window, `lv` and `cv` as
`double` quotients and `sv` as a `float` quotient
(`iqa/ssim_accumulate_lane.h`), and adds `lv * cv * sv`, `lv`, `cv` and `sv`
into one `double` each, window after window in raster order. It returns each
mean as `(float)(sum / windows)`.

The SYCL twin did two things differently
([ADR-1414](1414-sycl-float-ms-ssim-cpu-arithmetic.md)):

- **The terms.** A SYCL kernel has no fp64 type
  ([ADR-0220](0220-sycl-fp64-fallback.md)). The twin carried `lv` and `cv` as
  pairs of floats, about 2^-46 relative from the CPU's doubles, and rounded
  each term to an integer in units of 2^-52.
- **The order.** It added those integers per 16x8 work-group
  (`sycl::reduce_over_group`) and the group sums on the host. That sum is
  exact; the CPU's is a running `double` that rounds at every add.

The two sums differ by the rounding of the CPU's sequential adds, around
1e-16 relative, and the `float` rounding of the mean hides it unless the mean
lies that close to a rounding boundary. It does on frames whose terms cancel
(uncorrelated pictures): a search on independent 8-bit noise found 3 frames
in 2.7e7 at 64x64 and 10 in 2.0e7 at 176x176 with a differing `float_ssim`
or `float_ssim_l` (none with a differing `float_ssim_c` or `_s`). None of the 333 frames of real and stress
content behind ADR-1451 differs.

The maintainer decided the row: the twin adds the CPU's terms in the CPU's
order; neither a bound nor a change of the CPU reference.

## Decision

We will form the CPU's per-window terms on the device as the CPU's `double`
values, store them unreduced, and add them on the host in raster order.

**The CPU's doubles in integers.** `sycl_ssim_terms.h::ssim_double_terms()`
runs the reference's operations one for one on
`sycl_soft_signed.h` values (sign, 53-bit significand and exponent in
integers, each operation rounded to nearest, ties to even;
[ADR-1443](1443-sycl-ssim-cpu-arithmetic.md)):

- `lv = (2.0 * rm * cm + C1) / l_den`: the product of the two converted
  `float` means is exact (48 significant bits), doubling is exact, the sum
  with `C1` rounds once and the quotient by the converted `float`
  denominator once, as on the CPU.
- `cv = (2.0 * srsc + C2) / c_den`: one rounded sum and one rounded quotient.
- `sv` stays the `float` quotient it is on the CPU.

The fp32 part before it (clamped variances, `srsc`, the two denominators) is
unchanged and now a function of its own, `ssim_float_parts()`.

**No reduction on the device.** One work-item per window:

- Default: `FloatSsimTermKernel` stores the fp64 bit pattern of
  `(lv * cv) * sv` (`ssim_product_bits()`) at the window's raster position,
  8 bytes per window. The host adds the plane into one `double` in index
  order (`frame_sum_of_terms()`, the function `integer_ssim_sycl` already
  uses).
- `enable_lcs`: `FloatSsimLcsKernel` stores the bit patterns of `lv` and
  `cv` and the `float` `sv`, 20 bytes per window. The host forms
  `lv * cv * sv` and the four sums with the reference's own statements
  (`ssim_frame_sums()` / `accumulate_window()`).

The means are `(float)(sum / windows)` as before.

**Kernel shape.** SIMD-16 sub-groups with the 256-entry register file
(`VmafSyclKernelShape<16, 256>`), the per-window function flattened into the
kernel and the header's functions always inlined, as for the `ssim` twin.
`test_sycl_kernel_scratch` audits 127 kernels on the Arc A380: none uses
scratch memory ([ADR-1395](1395-sycl-kernels-no-scratch.md)).

**Every sum the extractor forms is covered**: `float_ssim`, and
`float_ssim_l`, `_c`, `_s` under `enable_lcs`, at every `scale` (the
decimation before the window pass was bit-exact already,
[ADR-1370](1370-sycl-float-ssim-device-decimation.md)).

`float_ms_ssim_sycl` keeps the pair terms and the work-group sums of
ADR-1414 in this change; see the follow-ups.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| **Per-window terms read back, host adds in raster order (chosen)** | The CPU's value by construction, for every sum and sign | 8 or 20 bytes per window read back; one dependent add per window on the host | The simple exact form first, as ADR-1443 and ADR-1424 |
| Keep the exact integer sum (ADR-1414) | No read-back; the sum has no rounding at all | It is not the CPU's sum: the CPU's running `double` rounds, and the mean differs by one float step on such frames | The twin reproduces the CPU; decided in the row |
| Keep the pair terms, change only the order | Smaller kernel change | The terms are not the CPU's doubles (about 2^-46 relative), so the sum still differs in its last bits | Would leave a second, smaller source of the same defect |
| `ordered_sum.h` on the device ([ADR-1433](1433-cuda-ssimulacra2-cpu-sum-order.md)): the bits of a sequential sum from per-binade integer sums | No read-back for that sum | Requires non-negative terms: `lv` and `cv` qualify, `sv` and `lv * cv * sv` are negative where a window's covariance is | Tuning candidate for the `lv` and `cv` sums (12 bytes per window instead of 20 under `enable_lcs`); `T-SYCL-FLOAT-SSIM-RASTER-SUM-THROUGHPUT-2026-10-02` |
| Form `lv * cv * sv` on the device under `enable_lcs` too | Two host multiplications less per window | 28 bytes per window instead of 20; the read-back is the largest part of the cost | The host product is the reference's own expression and cheaper than 8 more bytes |
| The sequential sum on the device | No read-back | One dependent fp64 add per window on one device thread, in integers | Orders of magnitude slower than the host |
| Change the CPU to an order-independent sum | Every twin could match cheaply | Moves CPU `float_ssim` scores on such frames; has to hold the Netflix golden gate | Decided in the row: not a CPU change ([ADR-0024](0024-netflix-golden-preserved.md)) |
| Take the twin off the exact list with a one-float-step bound | No code change | A bound where an exact twin is possible | Decided in the row: not a bound |

## Consequences

- **Positive**: the constructed pair scores `0xb4e2b622` on the Arc A380,
  with and without `enable_lcs`, as the CPU does (before: `0xb4e2b621`).
  Five noise pairs from the search that differed on the old twin are
  identical now (64x64 seeds 17217594 and 26198411 and 176x176 seeds 138433
  and 514849 in `float_ssim`, 64x64 seed 19119610 in `float_ssim_l`).
- **Positive**: measured on an Arc A380 (xe, Level Zero, icpx 2026.0) at
  `--precision max` against a GCC build of the CPU extractor, on 138 frames
  (Netflix 576x324 at 8, 10, 12 and 16 bits and as 10-bit 4:2:2, both
  1920x1080 checkerboard pairs, full-range noise at four bit depths, a bright
  16-bit 1920x1080 pair, BBB 3840x2160 widened to 16 bits, 50 frames of BBB
  3840x2160): every value identical under six option sets (default,
  `enable_lcs`, `scale=1`, `enable_lcs` with `scale=1`, `scale=3`,
  `enable_lcs` with `scale=2`), 2070 of 2070. The gate, against the CPU
  extractor of the same icx build, reports 0 for `float_ssim` and
  `float_ssim_lcs` on 333 of 333 frames (the 14 fixtures of ADR-1451, BBB
  with 200 frames).
- **Positive**: the twin is exact by construction now, not up to the `float`
  rounding of the mean as ADR-1451 had to say.
- **Positive**: `test_sycl_float_ssim_parity` compares with `==` (it allowed
  5e-4), runs the constructed pair and three search pairs on the device and,
  without a device, the kernels' arithmetic and the host's sums on those
  frames and 24 more noise frames
  (`vmaf_sycl_float_ssim_host_means()`). On the old twin it fails with
  `constructed pair, sycl float_ssim: 0xb4e2b621, enable_lcs 0xb4e2b621`.
- **Negative**: with the automatic scale the scored plane is at most 480x270
  and the cost is small: 1.51 ms per 1920x1080 frame instead of 1.32 and 4.11
  ms per 3840x2160 frame instead of 3.93 (1.60 instead of 1.34 and 4.35
  instead of 4.25 with `enable_lcs`). Where the scale is 1, every window of
  the frame is a term: 0.86 ms instead of 0.56 at 576x324, 9.9 ms instead of
  5.9 at 1920x1080 with `scale=1` (12.4 instead of 6.5 with `enable_lcs`) and
  39.1 ms instead of 23.3 at 3840x2160 with `scale=1` (48.6 instead of 25.5
  with `enable_lcs`), a factor 1.7 and 1.9. Medians of 7 interleaved runs of
  50 frames, host load average 22 to 26; the `psnr_sycl` control, the same
  code in both builds, read 2.93 and 2.92 ms. (A first set under a load
  average of 62 to 88 read the same factors within its noise, 1.7 and 2.1.)
- **Negative**: where the time at 3840x2160 with `scale=1` goes (8.2 million
  windows), from a second set of runs (load average 14 to 20) with builds
  that leave a stage out: 22.5 ms before, 29.5 ms with the new kernel alone
  (7.0 ms: the terms in integer arithmetic in place of pairs and a group
  reduction), 35.7 ms with the read-back of 66 MB (6.2 ms), 38.4 ms with the
  host's adds (2.7 ms). With `enable_lcs`: 24.8, 29.4 (kernel, 4.6 ms), 44.1
  (read-back of 165 MB, 14.7 ms) and 48.2 ms (the host's four sums and two
  products per window, 4.1 ms).
  `T-SYCL-FLOAT-SSIM-RASTER-SUM-THROUGHPUT-2026-10-02`.
- **Negative**: 8 bytes per window of device memory and of pinned host
  memory (20 with `enable_lcs`): 66 MB (165 MB) at 3840x2160 with `scale=1`,
  1 MB (2.4 MB) at the automatic scale's 480x270.
- **Neutral / follow-ups**:
  - `float_ms_ssim_sycl` has the same defect. It calls the same
    `iqa_ssim()` per scale and its twin adds pair terms per work-group. A
    176x176 noise pair (seed 2437157) gives `float_ms_ssim_l_scale0`
    0.9884904623031616 on the CPU and 0.9884905219078064 on the twin, one
    float step. It gets the same fix in its own change; `ssim_terms()`,
    `term_fixed()` and `FixedSum` stay in `sycl_ssim_terms.h` until then.
  - The header mirrors `iqa/ssim_accumulate_lane.h` and the frame means of
    `iqa/ssim_tools.c`. A change there changes `sycl_ssim_terms.h` in the
    same PR; `test_sycl_float_ssim_exact_contract.py` fails when the lines
    move.
  - `enable_db` is `-10 * log10(1 - ssim)` on the host, where an icx build
    links Intel's `log10` and a GCC build glibc's
    (`T-ICX-LIBIMF-HOST-MATH-2026-10-01`); the twin equals the CPU extractor
    of its own build.

## References

- `req` (maintainer decision relayed in the row's brief, 2026-10-02): "the twin adds the CPU's terms in the CPU's order. Not a bound, not a CPU change."
- `req` (maintainer brief for the SYCL exactness lane, 2026-10-01): "results before speed; a twin reproduces the CPU bit for bit, tuning comes afterwards".
- [ADR-1443](1443-sycl-ssim-cpu-arithmetic.md) (the `ssim` twin: soft fp64
  terms, host sum), [ADR-1414](1414-sycl-float-ms-ssim-cpu-arithmetic.md)
  (the pair terms and integer sums this replaces for `float_ssim`),
  [ADR-1451](1451-sycl-exact-twins-declared.md),
  [ADR-1370](1370-sycl-float-ssim-device-decimation.md),
  [ADR-1424](1424-cuda-ssim-cpu-frame-sum.md),
  [ADR-1433](1433-cuda-ssimulacra2-cpu-sum-order.md),
  [ADR-1428](1428-exact-twins-fragments.md),
  [ADR-1395](1395-sycl-kernels-no-scratch.md),
  [ADR-0220](0220-sycl-fp64-fallback.md),
  [ADR-0024](0024-netflix-golden-preserved.md).
- `docs/state.md`: `T-GPU-FLOAT-SSIM-FRAME-SUM-ORDER-2026-10-02` (the SYCL
  `float_ssim` part closed by this decision),
  `T-SYCL-FLOAT-SSIM-RASTER-SUM-THROUGHPUT-2026-10-02`.
