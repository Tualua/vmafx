<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1441: `float_ssim_hip` forms its window sums through the arithmetic `float_ms_ssim_hip` shares with the CPU and returns the CPU's score bit for bit

- **Status**: Accepted
- **Date**: 2026-10-01
- **Deciders**: lusoris
- **Tags**: `hip`, `ssim`, `gpu-parity`, `numerics`, `testing`, `ci`, `rc3`, `fork-local`

## Context

`float_ssim` and `float_ms_ssim` run the same core on the CPU, `iqa_ssim()`:
`iqa_convolve()` filters five planes with the separable 11-tap Gaussian, and
`ssim_accumulate_default_scalar()` forms `l`, `c` and `s` per window. The
convolution multiplies each tap in fp32, adds the eleven products in fp64 and
rounds to fp32 once per pass.

`float_ssim_hip` formed `l`, `c` and `s` in the CPU's types since
[ADR-1382](1382-hip-twin-cpu-option-parity.md) and decimates as the CPU does
since [ADR-1405](1405-hip-float-ssim-device-decimation.md). Its two
convolution passes added the products in an fp32 running sum, which rounds at
every tap. Measured on a gfx1036 at `--precision max` against the CPU on
`origin/master` 80c5a0332 (the RC3 sweep, #1772): 27 of 178 frames had the
CPU's score, the others were up to 4.8e-7 away. With `enable_lcs`,
`float_ssim_l` was identical on every frame and `float_ssim_c` / `float_ssim_s`
were up to 5.4e-7 off: the window means were right, the window sums of
squares and products were not.

`float_ms_ssim_hip` had the same defect and removed it in
[ADR-1403](1403-cuda-strict-fp-every-kernel.md): its arithmetic lives in
`core/src/feature/hip/integer_ms_ssim/ms_ssim_arith.h`, which carries the
fp64 sum as an exact fp32 pair (a two-sum, six fp32 operations per add) and
is compiled by the kernels and by a host test that holds it against the CPU.

## Decision

We will compute `float_ssim_hip`'s window sums and its `l` / `c` / `s` through
that header: `vmaf_hip_ms_ssim_horizontal()` for pass 1,
`vmaf_hip_ms_ssim_vertical()` for pass 2 and `vmaf_hip_ms_ssim_lcs()` for the
terms. The kernel keeps no tap table, no window sum and no SSIM term of its
own. `float_ssim` and `float_ssim_lcs`: `hip` are declared exact twins.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| The shared header's exact fp32 pair sums (this ADR) | The CPU's values; one implementation of the window arithmetic for both twins, already held against the CPU by `test_hip_ms_ssim_arith` | Six fp32 operations per add instead of one: 17 % more time per 1920x1080 frame and 7 % per 3840x2160 frame at the default scale, 32 % at `scale=1` | Chosen |
| fp64 accumulators | Simplest to read | ADR-1403 measured them on this device: 299 instead of 173 ms per 3840x2160 `float_ms_ssim` frame | Slower than the pair |
| Keep fp32 sums and the 5e-5 tolerance | No cost | 151 of 178 frames differ from the CPU; the twin stays the one SSIM-family HIP twin that is not exact | RC3 asks for the bits |
| A second copy of the pair arithmetic in the float_ssim kernel | No cross-directory include | Two copies of arithmetic that must both follow the CPU | One behaviour, one implementation |

## Consequences

- **Positive**: measured on a gfx1036 at `--precision max`, every output of
  every frame equals `--backend cpu`: 178 of 178 for `float_ssim` (27 before)
  and 712 of 712 with `enable_lcs` (Netflix 576x324 at 8, 10, 12 and 16 bits
  and as 10-bit 4:2:2, both 1920x1080 checkerboard pairs, Sparks 480x270 at
  10 bits, 48 frames of BBB 3840x2160, full-range noise at four depths, a
  bright 16-bit 1080p pair). Also with `scale=1`, `scale=3`, `enable_db` and
  `enable_db` plus `clip_db` on five fixtures.
- **Positive**: the kernel has no window arithmetic of its own; a change to
  `iqa_convolve()` or to the accumulate routine is one edit in the header for
  both twins.
- **Negative**: the twin is slower. Steady state on the gfx1036, medians of
  11 interleaved pairs of runs: 1.72 to 2.02 ms per 1920x1080 frame (+17 %)
  and 4.86 to 5.18 ms per 3840x2160 frame (+7 %) at the default scale (4 and
  8); with `enable_lcs` 1.98 to 2.31 ms at 1080p (+17 %); with `scale=1`
  17.7 to 23.4 ms at 1080p (+32 %) and 82.3 to 109.6 ms at 3840x2160 (+33 %).
  `T-HIP-FLOAT-SSIM-EXACT-THROUGHPUT-2026-10-01`.
- **Negative**: stored `float_ssim_hip` scores change by up to 4.8e-7.
- **Neutral / follow-ups**: the per-frame sum of the terms is fp64 in block
  order and the mean is rounded to fp32, as for `float_ms_ssim`; the rounding
  absorbs the order unless the sum lies within its own rounding error of an
  fp32 boundary (by estimate a few means in a million; the sweep in #1772
  states it for `float_ms_ssim`). Guards: `test_hip_float_ssim_parity` and its 960x540 registration
  (`==` on every score of every case; the first case fails on the old
  twin), `test_hip_ms_ssim_arith` (the header against the CPU, no device) and
  three planted regressions in `test_hip_kernel_source_contract.py`.

## References

- `req` (maintainer brief for the second HIP lane, 2026-10-01): "Not identical: find the cause [...], fix it the way the CUDA / SYCL twin of the same feature was fixed if one was (read that ADR and reuse its helpers [...])" and "note timing regressions above 5 % and stop to report if a fix costs more than 20 %."
- [ADR-1403](1403-cuda-strict-fp-every-kernel.md) (the shared arithmetic),
  [ADR-1382](1382-hip-twin-cpu-option-parity.md),
  [ADR-1405](1405-hip-float-ssim-device-decimation.md),
  [ADR-1397](1397-psnr-hvs-twins-cpu-float-sum.md) (the exact cell),
  [ADR-0214](0214-gpu-parity-ci-gate.md).
- `docs/state.md`: `T-HIP-FLOAT-SSIM-NOT-CPU-ARITHMETIC-2026-10-01`,
  `T-HIP-FLOAT-SSIM-EXACT-THROUGHPUT-2026-10-01`.
