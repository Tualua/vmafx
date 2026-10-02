<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1446: `ssimulacra2_sycl` forms the CPU's fp64 terms in 64-bit integers and returns the sums of the CPU's loops; it returns the CPU's score bit for bit

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `sycl`, `gpu-parity`, `numerics`, `ssimulacra2`, `testing`, `ci`, `rc3`, `fork-local`

## Context

Since [ADR-1363](1363-sycl-ssimulacra2-msssim-device-resident.md)
`ssimulacra2_sycl` runs the whole frame on the device and reproduces the CPU
extractor's fp32 planes bit for bit (colour conversion, XYB, blurs,
downsample). The last stage differed. `ssimulacra2.c::ssim_map()` and
`::edge_diff_map()` evaluate six terms per sample and channel in `double` and
add each into one `double`, pixel after pixel. A SYCL kernel has no fp64 type
([ADR-0220](0220-sycl-fp64-fallback.md)), so the twin evaluated the terms as
pairs of floats (relative error about 2^-44) and added the pairs in a fixed
tree.

Measured on an Arc A380 (xe driver) at `--precision max` against
`--backend cpu` of a GCC build, no frame equalled the CPU:

| Fixture | Frames | Identical | Max abs diff |
|---|---|---|---|
| Netflix 576x324, 8 bit | 48 | 0 | 1.1e-12 |
| Netflix 576x324, 10 bit | 3 | 0 | 7.5e-13 |
| Netflix 576x324, 12 bit | 3 | 0 | 7.3e-13 |
| Netflix 576x324, 16 bit | 3 | 0 | 8.5e-13 |
| Netflix 576x324, 10 bit 4:2:2 | 3 | 0 | 1.0e-12 |
| Checkerboard 1 px, 1920x1080 | 3 | 0 | 2.6e-13 |
| Checkerboard 10 px, 1920x1080 | 3 | 0 | 7.6e-11 |
| BBB 3840x2160 | 200 | 0 | 7.2e-13 |

[ADR-1433](1433-cuda-ssimulacra2-cpu-sum-order.md) made the CUDA twin exact
and [ADR-1445](1445-hip-ssimulacra2-cpu-sum-order.md) the HIP twin: both
devices have fp64, so they evaluate the CPU's expressions and form the sums
with `core/src/feature/ordered_sum.h`, which returns the bits of a sequential
loop from integer increments per binade. Both left the SYCL twin open
(`T-GPU-SSIMULACRA2-SUM-ORDER-2026-10-01`) with two things needed: the term
has to be the CPU's double, which an fp32 pair is not, and the ordered sum
needs fp64 for its plan and for the adds of the chunks it walks term by term.

The direction for the GPU twins is results first, bit for bit, speed
afterwards.

## Decision

We will make `ssimulacra2_sycl` return the CPU extractor's score bit for bit
without an fp64 type on the device.

**The CPU's terms, in integers.**
`core/src/feature/sycl/sycl_ssimulacra2_math.h` runs the reference's
operations one for one on fp64 values held as a sign, a 53-bit significand
and an exponent (`sycl_soft_signed.h`,
[ADR-1443](1443-sycl-ssim-cpu-arithmetic.md)), from the fp32 values the
reference converts, and returns fp64 bit patterns:

- `ssim_terms()`: the product of the two converted floats (exact in fp64),
  one quotient, `1.0 - ratio`, the clamp at zero, and `quartic()` as two
  squarings;
- `edge_terms()`: `fabs((double)a - (double)b)` twice, `1.0 + ed`, one
  quotient, `- 1.0`, the split into artifact and detail, and `quartic()`.

A zero denominator and a term the reference computes as an infinity or a NaN
are handled as the reference does for the frame: a clamped infinity is zero,
anything not finite becomes a NaN that the sum keeps and the frame guard
rejects.

**The CPU's sums.** `ordered_sum.h` gains `_bits` forms of its functions
(the fp64 forms are now wrappers around them, with the same results) and a
switch, `VMAF_ORDSUM_NO_FP64`, under which the header names no `double`.
`core/src/feature/sycl/sycl_ordered_sum.h` adds what a device without fp64
and with slow single lanes needs:

- the plan comes from fp32 advice: the old twin's pair terms, added per chunk
  of 512 consecutive pixels in an fp32 tree. A fourth power is scaled by 2^88
  first, since it lies far below the fp32 range. The plan is advice only: the
  walk adds a chunk from its increment only when
  `vmaf_ordsum_add_chunk_bits()` accepts it at the exact sum;
- the adds the walk does itself are `add_bits()`: the term's integer
  increment while the sum stays in its binade, and the fp64 addition in
  integers (`signed_add`) for the add that leaves it;
- a chunk whose plan says "term by term" gets one of 128 slots per sum. Its
  terms are kept, and their increments are composed into runs of 16 under the
  two binades the chunk is expected to end in. The walk then adds such a chunk
  as runs, the 16 terms of the run that crosses a binade, and runs again: a
  few dozen steps on its one lane instead of 512 fp64 additions in integers.
  A chunk beyond the slots has its terms computed by the walk.

**Kernels.** Per scale: `launch_chunk_sums` (advice), `launch_chunk_plan`
(18 work-items, one per sum), three unit kernels (the two SSIM sums, the four
edge sums, the slots) and `launch_ordered_totals` (one lane per sum). All are
scratch-free on the A380 ([ADR-1395](1395-sycl-kernels-no-scratch.md)): the
unit kernels at SIMD-16, the slot kernel with the 256-entry register file,
the walk at SIMD-8. The read-back stays one 864-byte block, now 108 fp64 bit
patterns.

**Gate.** `scripts/ci/exact_twins.d/ssimulacra2.sycl` declares the twin
exact: the cell is compared with tolerance 0 instead of `5e-3`.

## Alternatives considered

Times are per 3840x2160 and per 576x324 frame on the A380; the old twin took
84.1 and 5.31 ms.

| Option | Result | Verdict |
|---|---|---|
| Keep the fp32 pairs, add them in the CPU's order | Not exact: up to 1.1e-12 on the Netflix pair and 2.7e-12 on the 10 px checkerboard; 1 of 74 frames identical | Rejected |
| The exact terms, added in another order | Not exact: with 512-pixel chunk sums added in order, up to 1.3e-13 on the Netflix pair and 7.2e-11 on the 10 px checkerboard; 6 of 56 frames identical. The CUDA twin's tree gave the same figures (ADR-1433) | Rejected |
| Read the terms back and add them on the host, as `integer_ssim_sycl` does (ADR-1443) | 1.2 GB per 3840x2160 frame at scale 0 (six terms, three channels, 8.3 million pixels, 8 bytes) | Rejected |
| Exact terms twice per scale, once for the plan's sums and once for the increments, as the CUDA and HIP twins do | Not built: the unit stage costs 82 ms per 3840x2160 frame, the fp32 advice 13 ms, and the result cannot differ since the plan is advice | Rejected |
| The walk adds every unplanned chunk term by term, computing the terms itself | Bit-identical; 410.5 and 78.5 ms | Rejected: a lane of the A380 takes about a microsecond per fp64 step |
| The terms of such chunks kept by a parallel kernel, the walk adds them one by one | Bit-identical; 265 and 19.1 ms | Rejected: slower than runs |
| Runs of 16 under the binade the chunk starts in and the next, chunks of 256 | Bit-identical; 231 and 13.8 ms | Rejected: slower than chunks of 512 |
| **Chunks of 512, runs under the two binades the chunk ends in** | Bit-identical; 195.5 and 13.6 ms (194.7 and 13.5 in the final build's 7 runs) | **Chosen** |
| Chunks of 1024 | Bit-identical; 188.3 and 16.7 ms | Rejected: 2 % faster at 3840x2160, 22 % slower at 576x324 |
| SIMD-32 unit kernels, or the slot kernel at the default register file with chunks of 1024 | Register spills (scratch memory): wrong values under xe and runs that did not finish in 300 s | Rejected (ADR-1395) |

## Consequences

- **Positive**: measured on an Arc A380 at `--precision max`, the score of
  every frame equals `--backend cpu` of a GCC build and of the same icx
  build: 266 of 266 on the eight fixtures above (0 before), and 153 of 153
  with `yuv_matrix` 1, 2 and 3 on the Netflix pair and the 10 px
  checkerboard. Stored `ssimulacra2_sycl` scores change by up to 7.6e-11.
- **Positive**: the sums no longer depend on the tree a device's work-group
  size gives; every device returns the CPU's score.
- **Negative**: the twin takes 2.3 to 2.5 times as long: 194.7 ms instead of
  84.1 per 3840x2160 frame and 13.5 instead of 5.31 per 576x324 frame (medians
  of 7 runs of the `vmaf` tool, `(t(22) - t(2)) / 20` on BBB and
  `(t(48) - t(2)) / 46` on the Netflix pair; a `float_psnr_sycl` control took
  3.26 and 3.27 ms). The CPU extractor takes about 125 ms per 3840x2160 frame
  and 1.4 ms per 576x324 frame on sixteen threads (360 ms per 3840x2160 frame
  on one), so on this card the CPU is now the faster path at 3840x2160 as
  well; at 576x324 it already was. Where the time goes, from builds with
  stages removed (3840x2160 / 576x324; the full build read 191.5 / 13.58 in
  that series): the frame without the sums 69.1 / 4.81 ms; fp32 advice sums
  12.8 / 0.33; plan 7.9 / 0.23; unit kernels 81.7 / 2.74; walk 20.0 / 5.47.
  The old combine stage took 14.9 / 0.52.
  `T-SYCL-SSIMULACRA2-EXACT-THROUGHPUT-2026-10-02`.
- **Negative**: 18 MB more device memory at 3840x2160 (the plan, the
  increments, the kept terms and runs) and six kernels per scale instead of
  two.
- **Neutral / follow-ups**:
  - The host's final `pow()` is the build's libm. The twin and the CPU
    extractor of one build always agree; against the GCC build every
    measured frame agreed as well, and a difference there would be the
    host-libm class of `T-ICX-LIBIMF-HOST-MATH-2026-10-01`, not the twin.
  - `ordered_sum.h` is shared with the CUDA and HIP twins. Its fp64 forms
    are unchanged in result: `test_ordered_sum` passes, and on an RTX 4090
    `test_cuda_ssimulacra2_parity`, its `_large` variant and the gate cell
    stay at 0 with this change.
  - The arithmetic needs terms that are non-negative or NaN (ADR-1433).
  - A change to `ssim_map()` or `edge_diff_map()` in `ssimulacra2.c` changes
    `sycl_ssimulacra2_math.h` and `reference_terms()` of its test in the
    same PR.
  - Guards: `test_sycl_ssimulacra2_math` (600 000 samples against the
    reference's lines, host and device), `test_sycl_ordered_sum` (360 cases
    of term kind, length and plan, wrong plans included, against the loop;
    the walk on the host and in a kernel), `test_sycl_ssimulacra2_parity` and
    `_large` (16 score cases, `==`; 15 differ on the old twin),
    `test_sycl_ssimulacra2_exact_contract.py` (21 planted regressions, no
    device), `test_sycl_kernel_scratch`.

## References

- `req` (coordinator brief for the SYCL exactness lane, 2026-10-01): "User direction: results before speed; a twin reproduces the CPU bit for bit, tuning comes afterwards."
- `req` (same brief): "Then measure every other SYCL twin against the CPU at --precision max on the A380 (Netflix pair, both 1080p checkerboards, testdata/bbb 4K), list which are bit-identical and which are not with the max abs diff, and fix the non-identical ones one PR each in order of the largest difference, isolating the cause per term (types, rounding points, libm calls, reduction order)."
- `req` (coordinator, cost rule, 2026-10-02): "a cost above 20 % gets its tuning row in the same PR"
- [ADR-1433](1433-cuda-ssimulacra2-cpu-sum-order.md),
  [ADR-1445](1445-hip-ssimulacra2-cpu-sum-order.md),
  [ADR-1443](1443-sycl-ssim-cpu-arithmetic.md),
  [ADR-1432](1432-sycl-integer-vif-exact-gain.md),
  [ADR-1363](1363-sycl-ssimulacra2-msssim-device-resident.md),
  [ADR-1395](1395-sycl-kernels-no-scratch.md),
  [ADR-0220](0220-sycl-fp64-fallback.md),
  [ADR-1428](1428-exact-twins-fragments.md),
  [ADR-1421](1421-rc3-rc8-candidate-map.md),
  [ADR-0214](0214-gpu-parity-ci-gate.md).
- [Research-1446](../research/1446-sycl-ssimulacra2-cpu-bits.md),
  [Research-1433](../research/1433-cuda-ssimulacra2-cpu-sum-order.md).
- `docs/state.md`: `T-GPU-SSIMULACRA2-SUM-ORDER-2026-10-01`,
  `T-SYCL-SSIMULACRA2-EXACT-THROUGHPUT-2026-10-02`.
