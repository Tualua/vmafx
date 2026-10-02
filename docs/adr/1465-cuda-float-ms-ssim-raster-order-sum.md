<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1465: `float_ms_ssim_cuda` adds the terms of every scale in the CPU's raster order, on the host

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `cuda`, `gpu-parity`, `numerics`, `ssim`, `testing`, `rc3`, `fork-local`

## Context

`float_ms_ssim_cuda` is declared an exact twin of the CPU `float_ms_ssim`
(`scripts/ci/exact_twins.d/float_ms_ssim.cuda`, `float_ms_ssim_lcs.cuda`,
[ADR-1457](1457-cuda-exact-twins-declared.md)). Its kernels follow the CPU's
arithmetic type for type
([ADR-1403](1403-cuda-strict-fp-every-kernel.md)). The order of its sums did
not: `ms_ssim.c` runs `iqa/ssim_tools.c::iqa_ssim()` once per scale, which
adds every window's `l`, `c` and `s` into one `double` each, left to right
and top to bottom, and returns each mean as a `float`; the twin added the
same terms per warp, per 16x8 block and then on the host. ADR-1403 and
ADR-1457 recorded that as absorbed by the `float` rounding of the mean, and
ADR-1457 stated the rule for the case that a mean ever differs: the twin adds
in raster order.

A mean differs. After `float_ssim` showed the same defect (ADR-1464, on
`fix/cuda-float-ssim-raster-order-sum`), a search over noise
at 176x176 found four frames in 8.32 million on which one per-scale `l` or
`c` mean is one float step from the CPU's, on the CUDA, HIP and SYCL twins
alike (`T-CUDA-FLOAT-MS-SSIM-FRAME-SUM-ORDER-2026-10-02`). On one of them the
difference reaches the score: `float_ms_ssim_c_scale0` 0.9923959374427795
against 0.9923959970474243 moves `float_ms_ssim` from 0.06886243290982871 to
0.0688624330951202. The HIP lane found a fifth frame on its device
(`float_ms_ssim_c_scale1` `0x3f7c49a0` on the CPU, `0x3f7c499f` on the twin,
`float_ms_ssim` 1.3e-9 off), and the CUDA twin returned the HIP value on
it. No `s` mean differed: `s` is a `float` widened to
`double`, and a sum of such terms is exact in a `double` unless the running
sum is far larger than a term.

## Decision

`float_ms_ssim_cuda` forms the three sums of every scale as the CPU does, in
the form of ADR-1464 and [ADR-1424](1424-cuda-ssim-cpu-frame-sum.md):

- `ms_ssim_vert_lcs` reduces nothing. It stores each window's `l` and `c` as
  doubles and its `s` as the `float` it is, at the window's raster index of
  three planes per scale.
- The host reads the planes back. `ms_ssim_scale_sums()` adds the three sums
  of a scale in one pass, each in index order, widening `s` as the CPU's
  `sv = sv_f` does.
- The per-scale means, their `float` rounding and the Wang product are
  unchanged.

The device test scores two pairs: the HIP lane's frame, kept as luma bytes
in the header the three twin tests share
(`core/test/float_ms_ssim_order_frame.h`), and one of the frames found on
CUDA, which the test rebuilds from a formula (`splitmix64` of the frame
index and the sample index), so that a second sum at another scale is held
without another 389 kB of data.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Terms read back, host adds in raster order (this ADR) | The CPU's sums by construction; the form of ADR-1424 and ADR-1464 | 20 bytes per window read back and three host adds per window: about 2.1 ns per window, on 1.33 windows per pixel | Chosen: results first |
| `s` as a `double` plane like `l` and `c` | One element type | 24 bytes per window instead of 20 for a value that is a `float` | The narrower plane is the same value and less to read back |
| Remove the fragments and bound the cells at one float step | No cost | The twin is then not the CPU's `float_ms_ssim` | ADR-1457 ruled this out in advance; maintainer decision for `float_ssim` |
| `core/src/feature/ordered_sum.h` on the device for `l` and `c`, which are non-negative ([ADR-1433](1433-cuda-ssimulacra2-cpu-sum-order.md)), and `s` exactly | No read-back for two of the three sums | Four more kernels per scale as in `ssimulacra2_cuda`, and `s` needs its own argument: its sum is exact only while no term is far smaller than the running sum | The tuning candidate in `T-CUDA-FLOAT-MS-SSIM-EXACT-THROUGHPUT-2026-10-02` |
| Keep the block sums and prove each rounded mean from them, reading the terms back only when the proof fails | Read-back on a few frames in a hundred | A second code path and an error bound to argue and test | Recorded as a tuning candidate in the same row |

## Consequences

- **Positive**: on the frame of `float_ms_ssim_order_frame.h` the twin
  returns the CPU's sixteen outputs, `float_ms_ssim_c_scale1` `0x3f7c49a0`
  among them, and on the formula frame `float_ms_ssim_l_scale0` `0x3f7cd999`
  (`test_cuda_float_ms_ssim_order`, which fails on the earlier twin with
  `float_ms_ssim_c_scale1: cpu=0.98549842834472656 (0x3f7c49a0)
  cuda=0.98549836874008179 (0x3f7c499f)`). The three other frames the search
  found are identical too.
- **Positive**: measured on an RTX 4090 at `--precision max` against
  `--backend cpu` of the same build: 15 750 of 15 750 values identical, `float_ms_ssim` alone and with
  `enable_lcs`, `enable_db` and `clip_db`, on the typical set (Netflix
  576x324 at 8 and 10 bits, both 1080p checkerboards, 200 frames of BBB
  3840x2160; 10 675 values) and the stress set (Netflix at 12 and 16 bits
  and as 10-bit 4:2:2, Sparks, noise at four bit depths, a bright 16-bit
  1080p pair, 16-bit BBB at 1080p and 4K; 5075 values). 60 000 noise frames
  at 176x176 with `enable_lcs`, the chunk that held the fixture frame among
  them: 960 000 of 960 000. The gate cells `float_ms_ssim` and
  `float_ms_ssim_lcs` report 0 at tolerance 0 on the Netflix pair and on 200
  BBB frames.
- **Negative**: time. Per frame through the `vmaf` tool, medians of 11
  alternating pairs against the build before the change, host load average
  12 to 15:

  | Input | Windows, five scales | Before | After | Paired difference |
  |---|---:|---:|---:|---:|
  | 576x324 | 231 702 | 0.33 ms | 0.81 ms | +0.49 ms |
  | 1920x1080 | 2 704 530 | 2.70 ms | 8.80 ms | +6.06 ms |
  | 3840x2160 | 10 932 650 | 10.92 ms | 33.71 ms | +22.97 ms |

  With `enable_lcs` the times are the same within their spread (0.36 to
  0.85 ms, 2.53 to 8.33 ms, 10.89 to 33.95 ms): the option only publishes
  means the twin forms in any case. That is 2.5 to 3.3 times the time, about
  2.1 ns per window, for the read-back of 20 bytes and three dependent adds
  per window. `T-CUDA-FLOAT-MS-SSIM-EXACT-THROUGHPUT-2026-10-02`.
- **Negative**: memory. 20 bytes per window on the device and as pinned host
  memory: 4.6 MB at 576x324, 54 MB at 1920x1080, 219 MB at 3840x2160. The
  per-block partials they replace were at most 2 MB.
- **Negative**: a stored `float_ms_ssim_cuda` score can change in its last
  digits (1.9e-10 on the frame above) where a per-scale mean lay within its
  sum's rounding error of a float boundary; none of the measured content
  frames does.
- **Neutral / follow-ups**:
  - The HIP and SYCL twins have the same defect
    (`T-CUDA-FLOAT-MS-SSIM-FRAME-SUM-ORDER-2026-10-02`) and are fixed by their
    own lanes; the frame header is shared so the three tests score the same
    bytes.
  - What is left of ADR-1403's caveat is the window sums, which the kernel
    carries as an fp32 pair of 48 bits where `iqa_convolve()` uses a
    `double`. No measured frame shows it.
  - The contract mirrors `iqa_ssim()`'s accumulators. A change to the terms
    or to the order of the sums changes the kernel and `ms_ssim_scale_sums()`
    in the same PR. `test_cuda_float_ms_ssim_exact_contract.py` pins the
    design without a device.

## References

- `req` (coordinator brief, 2026-10-02, `w21-float-ssim-raster.md`, item 5):
  "`float_ms_ssim` on your backend: same construction question (is its frame
  sum in the CPU's order?). Read the code and answer yes or no with the
  lines. If no: spend at most one hour constructing a failing frame [...];
  found -> fix it in a second PR the same way".
- ADR-1464 (`float_ssim_cuda`, on `fix/cuda-float-ssim-raster-order-sum`),
  [ADR-1424](1424-cuda-ssim-cpu-frame-sum.md),
  [ADR-1457](1457-cuda-exact-twins-declared.md),
  [ADR-1403](1403-cuda-strict-fp-every-kernel.md),
  [ADR-1433](1433-cuda-ssimulacra2-cpu-sum-order.md),
  [ADR-1428](1428-exact-twins-fragments.md).
