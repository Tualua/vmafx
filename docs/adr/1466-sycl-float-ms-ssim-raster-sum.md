<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1466: `float_ms_ssim_sycl` stores every window's `l`, `c` and `s` of every scale and adds them on the host in the CPU's raster order

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `sycl`, `gpu-parity`, `numerics`, `ssim`, `testing`, `rc3`, `fork-local`

## Context

`float_ms_ssim_sycl` is a declared exact twin
(`scripts/ci/exact_twins.d/float_ms_ssim.sycl`, `float_ms_ssim_lcs.sycl`,
[ADR-1414](1414-sycl-float-ms-ssim-cpu-arithmetic.md)). ADR-1414 said what was
left: the twin was "exact in practice, not by construction", with pair terms
about 2^-46 from the CPU's doubles and an exact integer sum where the CPU
keeps a running `double`.

[ADR-1463](1463-sycl-float-ssim-raster-sum.md) showed for `float_ssim` that
this is not exact: `iqa_ssim()` adds one `double` per window in raster order,
those adds round, and on a frame whose mean lies next to a `float` rounding
boundary another sum of the same windows returns the neighbouring `float`.
`ms_ssim.c` calls the same `iqa_ssim()` once per scale and uses its `l`, `c`
and `s` means.

A search for `float_ssim` found a 176x176 noise pair (seed 2437157 of
`core/test/ssim_order_noise.h`) on which the twin's `float_ms_ssim_l_scale0`
is 0.9884905219078064 (`0x3f7d0db7`) and the CPU's 0.9884904623031616
(`0x3f7d0db6`). The combined `float_ms_ssim` is equal on that pair, because
the luminance of scale 0 has exponent 0 in the product; the mean is an output
under `enable_lcs`, and a `c` or `s` mean that moves does change the score.

The HIP lane found a second pair (`core/test/float_ms_ssim_order_frame.h`) on
which `float_ms_ssim_cuda` and `float_ms_ssim_hip` return `0x3f7c499f` for
`float_ms_ssim_c_scale1` where the CPU returns `0x3f7c49a0`. The SYCL twin of
ADR-1414 returned the CPU's bits there: its integer sum has no rounding of
its own, and on that pair the exact sum rounds to the same `float` as the
CPU's running sum. On the first pair it does not. An exact sum is independent
of the order and still is not the CPU's sum.

## Decision

We will give `float_ms_ssim_sycl` the terms and the sums of ADR-1463.

- The vertical kernel forms `lv` and `cv` of each window as the CPU's doubles
  in 64-bit integers (`sycl_ssim_terms.h::ssim_double_terms()`) and `sv` as
  the CPU's `float` quotient, and stores all three at the window's raster
  position: 20 bytes per window, for every scale of every scored plane. There
  is no reduction on the device.
- `submit()` enqueues every scale and two copies of the whole frame's terms
  (the fp64 patterns and the `float` plane); `collect()` waits once, as
  before ([ADR-1363](1363-sycl-ssimulacra2-msssim-device-resident.md)).
- The host adds each scale's windows in index order
  (`sycl_ssim_terms.h::ssim_lcs_sums()`: three sums, without the product
  `iqa_ssim()` also forms and `ms_ssim.c` does not use), rounds each mean to
  `float` and combines the scales as before.
- Kernel shape SIMD-16 with the 256-entry register file, the window function
  flattened into the kernel, as in `float_ssim_sycl`. `test_sycl_kernel_scratch`
  audits 128 kernels on the Arc A380: none uses scratch memory
  ([ADR-1395](1395-sycl-kernels-no-scratch.md)).

The pair terms, the fixed-point conversion and the exact host sum
(`ssim_terms()`, `ssim_term()`, `term_fixed()`, `FixedSum`) have no user left
and leave `sycl_ssim_terms.h`.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| **Per-window terms of every scale read back, host adds in raster order (chosen)** | The CPU's value by construction for all fifteen means | 20 bytes per window read back: 219 MB per 3840x2160 frame over the five scales; 33 million dependent host adds | The simple exact form first, as ADR-1463 |
| Keep the exact integer sums of ADR-1414 | No read-back, 2 MB of partials at 3840x2160 | Not the CPU's sums: one float step off on the first pair above | The twin reproduces the CPU |
| Without `enable_lcs`, skip `l` for scales 0 to 3 | Their exponent in the product is 0, so the score does not use them: 12 bytes per window instead of 20 and one soft division less | A second kernel and layout; `l` is still needed under `enable_lcs` | Tuning candidate, `T-SYCL-FLOAT-MS-SSIM-RASTER-SUM-THROUGHPUT-2026-10-02` |
| The `l` and `c` sums from per-binade integer sums on the device (`ordered_sum.h`, [ADR-1433](1433-cuda-ssimulacra2-cpu-sum-order.md)) | Both terms are positive, so the device can return the bits of the sequential sum: 4 bytes per window left to read | A plan, per-chunk sums and a fallback per sum and scale, as `ssimulacra2_sycl` has ([ADR-1446](1446-sycl-ssimulacra2-cpu-bits.md)) | Tuning candidate, same row |
| One double per window, as the HIP twin does (#1836) | 8 bytes where a sum is read back alone | The three sums need three planes either way; `s` is a `float` on the CPU, so 4 bytes hold it exactly | Same idea; 20 bytes is its SYCL form |

## Consequences

- **Positive**: on the first pair the twin returns the CPU's `0x3f7d0db6`
  for `float_ms_ssim_l_scale0` (before: `0x3f7d0db7`), and all 16 luma
  outputs equal the CPU's. On the second pair it returns the CPU's
  `0x3f7c49a0` for `float_ms_ssim_c_scale1`, as it did before.
- **Positive**: measured on an Arc A380 (xe, Level Zero, icpx 2026.0) at
  `--precision max` against a GCC build of the CPU extractor, on 138 frames
  (Netflix 576x324 at 8, 10, 12 and 16 bits and as 10-bit 4:2:2, both
  1920x1080 checkerboard pairs, full-range noise at four bit depths, a bright
  16-bit 1920x1080 pair, BBB 3840x2160 widened to 16 bits, 50 frames of BBB
  3840x2160): `float_ms_ssim` 138 of 138, and with `enable_lcs` 2208 of 2208
  values. With `enable_chroma` on the 69 frames whose chroma planes clear the
  pyramid minimum: 206 of 207 values, and 1241 of 1242 with `enable_lcs`
  too. The one that differs is a `float_ms_ssim_cb` 1.1e-16 from the GCC
  build's; the CPU extractor of the icx build differs from the GCC build on
  the same value by the same amount: the scales are combined with `pow()` on
  the host, which is Intel's in an icx build and glibc's in a GCC build
  (`T-ICX-LIBIMF-HOST-MATH-2026-10-01`). The gate, against the CPU extractor
  of the same build, reports 0 for `float_ms_ssim` and `float_ms_ssim_lcs`
  on 333 of 333 frames of 14 fixtures.
- **Positive**: the twin is exact by construction, which ADR-1414 could not
  say.
- **Negative**: 75.9 ms per 3840x2160 frame instead of 44.7 (a factor 1.70),
  18.2 ms instead of 11.5 at 1920x1080 and 1.92 ms instead of 1.16 at
  576x324; with `enable_chroma` at 3840x2160 112.4 ms instead of 65.2.
  `enable_lcs` costs nothing more (76.0 and 19.2 ms): the default run stores
  the same terms. Medians of 7 interleaved runs of 25 frames, host load
  average 13 to 15; the `psnr_sycl` control read 2.95 and 2.92 ms. The CPU
  extractor takes 409 ms for the 3840x2160 frame on one thread and 58.7 ms
  with 16 threads (`scripts/dev/speed_gpu_parity.py`, which read 78.5 ms for
  the twin in the same run): at that size the twin is now slower than a
  16-thread CPU run, where it was faster before.
- **Negative**: where the time goes at 3840x2160 (10.9 million windows over
  the five scales), from builds with a stage left out, a second set of runs
  (load average 13): 41.9 ms before, 49.7 ms with the new kernel alone (7.8
  ms: two soft fp64 quotients per window in place of pairs and a group
  reduction), 70.4 ms with the read-back of 219 MB (20.6 ms), 75.1 ms with
  the host's three sums (4.8 ms).
  `T-SYCL-FLOAT-MS-SSIM-RASTER-SUM-THROUGHPUT-2026-10-02`.
- **Negative**: 20 bytes per window of device memory and of pinned host
  memory: 219 MB at 3840x2160 (327 MB with `enable_chroma` on 4:2:0), 54 MB
  at 1920x1080, where the partials took 2 MB at 3840x2160.
- **Neutral / follow-ups**:
  - `sycl_ssim_terms.h` mirrors `iqa/ssim_accumulate_lane.h` and the means of
    `iqa/ssim_tools.c` for both twins. A change there changes the header in
    the same PR; `test_sycl_kernel_source_contract.py` and
    `test_sycl_float_ssim_exact_contract.py` fail when the lines move.
  - The search that found the first pair looked at scale 0 (it was a search
    for `float_ssim`). No search has been run over the `c` and `s` means of
    the deeper scales on SYCL; the fix does not depend on one.

## References

- `req` (maintainer decision relayed in the row's brief, 2026-10-02): "the twin adds the CPU's terms in the CPU's order. Not a bound, not a CPU change."
- `req` (the brief's item on `float_ms_ssim`, 2026-10-02): "found -> fix it in a second PR the same way".
- [ADR-1463](1463-sycl-float-ssim-raster-sum.md) (the method, for
  `float_ssim_sycl`), [ADR-1414](1414-sycl-float-ms-ssim-cpu-arithmetic.md)
  (the window arithmetic this keeps and the sums it replaces),
  [ADR-1363](1363-sycl-ssimulacra2-msssim-device-resident.md) (one wait per
  frame), [ADR-1443](1443-sycl-ssim-cpu-arithmetic.md) (the soft fp64
  operations), [ADR-1433](1433-cuda-ssimulacra2-cpu-sum-order.md),
  [ADR-1446](1446-sycl-ssimulacra2-cpu-bits.md),
  [ADR-1395](1395-sycl-kernels-no-scratch.md),
  [ADR-0220](0220-sycl-fp64-fallback.md).
- `docs/state.md`: `T-GPU-FLOAT-SSIM-FRAME-SUM-ORDER-2026-10-02` (the SYCL
  `float_ms_ssim` part closed by this decision),
  `T-SYCL-FLOAT-MS-SSIM-RASTER-SUM-THROUGHPUT-2026-10-02`.
