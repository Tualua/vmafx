<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1497: The `float_moment` twins form the CPU's rounded second-moment sum past 2^53 units, and return the CPU's bits on every frame

- **Status**: Accepted
- **Date**: 2026-10-03
- **Deciders**: lusoris
- **Tags**: `cuda`, `sycl`, `hip`, `gpu-parity`, `numerics`, `float-moment`, `testing`, `rc3`, `fork-local`
- **Supersedes**: the deferral of a bit-identical sum past 2^53 units in [ADR-1447](1447-hip-float-moment-cpu-float-squares.md), [ADR-1449](1449-sycl-float-moment-cpu-float-squares.md) and [ADR-1453](1453-cuda-float-moment-cpu-float-squares.md). Their decision to add the CPU's float squares stands.

## Context

`moment.c::compute_2nd_moment()` adds one float square per pixel into a
`double`, in raster order, and divides by the pixel count. The CUDA, SYCL and
HIP twins add the same float squares as integers in units of 1 / scaler^2
(each below 2^32) and divide the exact sum on the host
([ADR-1447](1447-hip-float-moment-cpu-float-squares.md),
[ADR-1449](1449-sycl-float-moment-cpu-float-squares.md),
[ADR-1453](1453-cuda-float-moment-cpu-float-squares.md)). That is the CPU's
sum while it stays at or below 2^53 units, which covers every frame of up to
2^21 pixels at 16 bits and every 8-, 10- and 12-bit frame up to 2^29 pixels.
Past it the CPU's running sum rounds as it adds and the twins, which round
the exact sum once, were within a derived bound (2.7e-7 measured on a
2560x1440 frame; 0 of 16 frames of 16-bit 3840x2160 noise identical). The
three ADRs deferred the bit-identical form to
`T-HIP-FLOAT-MOMENT-PAST-2-53-2026-10-02`, and the maintainer decided that it
must be bit-identical on every input.

Two designs were on the table. The triage preferred replaying the CPU's loop
on the host when a frame's exact sum passes 2^53 (option B below), on the
host picture the twin has or reads back. None of the three twins has the host
picture when it learns the sum: `float_moment_cuda` receives device
pictures, `float_moment_hip` gets host pictures that the caller recycles when
`submit()` returns, and `float_moment_sycl` reads a device frame. `collect()`
of frame N - 1 runs after frame N is uploaded (`dispatch_gpu_double_buffer()`
in `libvmaf.c`), so a late read-back can see the next frame. The replay would
need a copy of both luma planes on every 16-bit frame above 2^21 pixels (50 MB
at 3840x2160), and on every frame that passes 2^53 a host loop as long as the
CPU extractor's own (17 ms per 16-bit 3840x2160 frame on this host).

## Decision

We will form the CPU's sum on the device from rows, with the arithmetic of
`feature/ordered_sum.h` ([ADR-1433](1433-cuda-ssimulacra2-cpu-sum-order.md))
on integer terms, in one header shared by the three twins and the host test,
`core/src/feature/float_moment_sum.h`:

- In units of 1 / scaler^2 every step of the CPU's loop is an integer, and
  the step is the exact integer sum rounded to 53 significant bits, ties to
  even (`vmaf_moment_sum_add_term()`). The scale is a power of two and the
  sums are normal doubles, so this is the CPU's double loop, value for value.
- While the running sum stays in one binade from 2^53 up it is a multiple of
  its last place, and adding a term moves it by the term rounded to that
  place, a tie to the even multiple; a run of terms is two integer increments,
  one for an even and one for an odd start, and runs compose associatively
  (`vmaf_ordsum_round_shifted()`, `vmaf_ordsum_then()`).
- Four kernels on a frame that can pass 2^53 units
  (`vmaf_moment_sum_may_round()`), after the frame kernel: each row's exact
  sum; a plan per row from the exact prefix of those sums (the exact range, or
  the binade the row is expected to stay in); each planned row's increments,
  one run per lane of 256 and an ordered tree; and one walk per plane. The
  walk adds a row exactly while the sum stays at or below 2^53, from its
  increments when the exact running sum is in the planned binade and stays in
  it, and otherwise as its 256 runs under the binade the sum is in and the
  next one; a run that crosses a binade is added term by term. A row adds
  less than 2^47 units and a binade from 2^53 up spans at least 2^53, so a row
  crosses at most one binade. The walk writes the CPU's sums into the
  accumulators the host already reads; `collect()` is unchanged. Every kernel
  returns at once when the plane's exact sum is at most 2^53.
- The CUDA and HIP twins compile the same four kernels from
  `core/src/feature/float_moment_sum_gpu.h`; the SYCL twin lays the same lane
  steps out in its own kernels, integers only and without scratch memory.

Why it is the CPU's double on every input: each step of the walk is either
the integer recurrence itself, an exact add of integers at or below 2^53, or
an increment that the add-chunk check of ADR-1433 accepts at the exact sum;
the plan only chooses which increments are computed, and any plan, even a
wrong one, sends the rows it does not fit to the runs or the terms.
`test_float_moment_sum` runs the kernels' steps on the host against
`picture_copy()` + `compute_2nd_moment()` on frames whose exact sum is
2^53 - 1, 2^53, 2^53 followed by three dropped ties, 2^54 followed by seven
dropped terms, and noise up to 7680x4320 (four binades), with real and with
deliberately wrong plans, and checks the one-term step against the double add
on 400 000 random pairs and every boundary pair.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Rows with integer increments and a checked walk on the device (this ADR) | The CPU's bits on every frame, by the argument above; the readback, `collect()` and the picture handling stay; nothing runs on frames that cannot pass 2^53; measured cost +0.03 ms (RTX 4090), +2.8 ms (Arc A380), +7.2 ms (gfx1036) per 16-bit 3840x2160 frame past 2^53 | Four more kernels and one shared header per twin; the integer-only and scratch-free rules for SYCL | Chosen |
| B: keep the exact sum and replay the CPU's raster loop on the host when it passes 2^53 | The CPU's double by construction; one host helper | No twin holds the host picture at `collect()` (see Context); an eager copy of both luma planes on every 16-bit frame above 2^21 pixels; a host loop of about the CPU extractor's 17 ms on every frame past 2^53, against the 5.3 ms the CUDA twin takes | The cost is not limited to the frames that need it, and the host time is the CPU extractor's own |
| A as in ADR-1433: `ordered_sum.h` on the terms as doubles, chunks of 1024 pixels, a plan from fp64 advice sums | The pattern of the ssimulacra2 twins | Terms converted to doubles and back for no gain; chunks across row ends; a SYCL twin needs the `_bits` path and the slots of ADR-1446 | The terms are integers: the exact row sums are the plan's prefix and the walk's exact range, and rows fit every backend's memory layout |
| Increments for every binade a frame can reach, in one pass, without a plan | Two kernels instead of four | Up to nine increments per term and plane; 16 or more 64-bit registers per lane, which spill on an Arc A380 (scratch memory, ADR-1395) | More work per term on the weakest devices, and a scratch risk |
| One work-item walking every term past 2^53 | Simplest | About 8 million dependent steps on one lane per 3840x2160 plane | Orders of magnitude slower than the frame |
| Keep the derived bound | No change | Not the CPU's bits | The maintainer decided against it (References) |

## Consequences

- **Positive**: the four outputs equal `--backend cpu` at `--precision max`
  on every measured frame on all three devices: 16 of 16 frames of 16-bit
  3840x2160 noise (0 of 16 before, 2.66e-7), 4 of 4 of 16-bit 7680x4320 noise
  (0 of 4 before, 5.1e-7), 32 of 32 of BBB 3840x2160 widened to 16 bits in
  three ways (shift by 8, times 257, full range with a dithered low byte; 17
  of the 32 past 2^53, identical before as well: those terms have no bits
  below the sum's last place under 2^55). The parity gate's `float_moment`
  cell reads 0 at tolerance 0 on the Netflix pair and on the 16-bit
  3840x2160 noise on each device (FAIL on master there). The exact-twin
  fragments drop their bound clause.
- **Negative**: per 16-bit 3840x2160 frame through libvmaf, pictures
  preloaded, medians of 5 interleaved runs at a load average of 4 to 5,
  before and after: RTX 4090 5.28 and 5.31 ms (noise, every frame past 2^53),
  5.31 and 5.29 ms (BBB full range, 17 of 32 past); Arc A380 31.84 and
  34.62 ms, 32.05 and 33.02 ms; gfx1036 18.40 and 25.62 ms, 18.37 and
  21.28 ms. The cost sits in the row-increment kernel, 64-bit composition
  per term, which the 2-CU gfx1036 and the A380's emulated 64-bit integers
  feel most: `T-GPU-FLOAT-MOMENT-EXACT-SUM-COST-2026-10-03` (RC8) records it.
  8-, 10- and 12-bit frames and frames up to 2^21 pixels run no new kernel.
- **Neutral / follow-ups**:
  - The twins match the CPU extractor where its SIMD path adds in raster
    order: the scalar loop and the AVX2 and AVX-512 paths do. The NEON and
    SVE2 paths of `moment.c` add in lanes, so past 2^53 units they are not
    the scalar sum, on the CPU or against a twin on an aarch64 host:
    `T-ARM-MOMENT-NEON-SVE2-SUM-ORDER-2026-10-03` (RC7).
  - `float_moment_metal` still adds integer squares
    (`T-GPU-FLOAT-MOMENT-16BIT-SQUARES-2026-10-02`); it would take this
    header once that row is closed.
  - Guards: `test_float_moment_sum` (host, the kernels' steps against the
    CPU; each of seven hand-made breaks of the header fails it: a tie to the
    odd value, the parity ignored, the tree composed in reverse, the units of
    a run under the wrong binade, the term-by-term fallback as exact adds,
    no rounding at all), `test_float_moment_sum_contract.py`
    (device-free, ten planted regressions), and
    `test_{cuda,sycl,hip}_float_moment_parity` (`==` on a 2560x1440 frame past
    2^53, a 4096x2560 frame past 2^55, and the three 2^53 boundary frames;
    each fails on master).

## References

- `Q` (maintainer popup, 2026-10-03, float_moment past 2^53 units): "Make it exact (Recommended)".
- `req` (coordinator brief for the RC3 exit lane, 2026-10-03): "make it bit-exact on every input".
- [ADR-1433](1433-cuda-ssimulacra2-cpu-sum-order.md) and [ADR-1446](1446-sycl-ssimulacra2-cpu-bits.md): the ordered-sum arithmetic and its SYCL constraints.
- [ADR-1395](1395-sycl-kernels-no-scratch.md), [ADR-0220](0220-sycl-fp64-fallback.md): no scratch memory, no fp64 in SYCL kernels.
- `docs/state.md`: `T-HIP-FLOAT-MOMENT-PAST-2-53-2026-10-02` (closed by this ADR).
