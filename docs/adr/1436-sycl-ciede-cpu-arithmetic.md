<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1436: `ciede_sycl` runs the CPU's statements on fp32 pairs and adds in the CPU's order; it lands where the CUDA twin does, 1.4e-11 from the CPU

- **Status**: Accepted
- **Date**: 2026-10-01
- **Deciders**: lusoris
- **Tags**: `sycl`, `gpu-parity`, `numerics`, `ciede`, `testing`, `ci`, `rc3`, `fork-local`

## Context

`ciede_sycl` matched the CPU `ciede` extractor on no frame of real content and
was up to 1.14e-5 from it on an Arc A380 at `--precision max` (Netflix
576x324; 1.3e-6 at 3840x2160). The gate allowed it `5e-3`
([ADR-0187](0187-ciede-vulkan.md)).

[ADR-1426](1426-cuda-ciede-cpu-arithmetic.md) found the cause on the CUDA
twin, which had the same kernel: `ciede.c` computes in double and stores in
float. `get_lab_color()` is fp64 up to the cube root, `ciede2000()` evaluates
almost every expression in fp64 and names the result `const float`, and
`extract()` adds every pixel into one double. The twins were fp32 throughout,
with float math functions, a rearranged formula and a sum per 16x16 block.
The CUDA twin was rewritten in the reference's types and is within 1.4e-11.

A SYCL kernel cannot be rewritten that way: it has no fp64 type
([ADR-0220](0220-sycl-fp64-fallback.md); one fp64 instruction blocks the
translation unit on Arc A-series). ADR-1426 therefore left the SYCL twin at
`5e-3` "unless an fp32-pair evaluation is written"
(`T-GPU-CIEDE-CPU-ARITHMETIC-2026-10-01`).

The direction for the GPU twins is results first, bit for bit, speed
afterwards; where the CPU's own math library is the obstacle, name what
differs and get everything else identical.

## Decision

We will evaluate `ciede.c`'s statements in the SYCL kernel with every fp64
value as an fp32 pair, round to float where the reference rounds, and add the
per-pixel values on the host in the reference's order.

**Pairs for fp64.** `core/src/feature/sycl/sycl_ciede_math.h` is
`get_lab_color()`, `ciede2000()` and their helpers, statement for statement
with the CUDA twin's `ciede_device.h`. A `double` of the reference is an
`Ff` (hi + lo, about 48 bits, `sycl_exact_fp.h`); a `float` of the reference
is a float, rounded from the pair at the statement where the reference
rounds. The reference's constants are built on the host from its own
expressions (`make_constants()`) and reach the kernel by value.

**Pair functions for the math library.**
`core/src/feature/sycl/sycl_ff_math.h` (new) has what `ciede.c` calls:

| Function | Method | Error measured |
|---|---|---:|
| `sqrt` | the device's fp32 root and one Newton correction from the exact residual | 2^-46 |
| `cbrt` (for `pow(c, 1.0 / 3.0)`) | one Halley step from the device's `cbrt` | 2^-45 |
| `pow_2_4` (for `pow(x, 2.4)`) | the square of `x` times its fifth root; the root by one Halley step from the device's `pow` | 2^-44 |
| `pow_7` | four multiplications | 2^-45 |
| `exp` | `2^k e^r`, `r` by a three-part `ln 2`, 14 Taylor terms | 2^-47 |
| `sin`, `cos` | `k pi / 16 + r` by a three-part `pi / 16`, a 32-entry table, 4 and 5 series terms | 2^-46 |
| `atan2` | a quotient in `[0, 1]`, a 17-entry table, 4 series terms | 2^-45 |

A Halley step cubes the error of its start, so the device's own `cbrt` and
`pow`, which are not correctly rounded, do not show in the result. The same
holds for a pair quotient and a pair root: what the device's fp32 division
or square root leaves is taken from the exact residual, so they use the
device's own operations where `sycl_exact_fp.h`'s `ff_div()` pays for two
correctly rounded divisions. The constants and tables are generated
(`scripts/dev/gen_sycl_ff_math.py`) and the tables are read from device
memory.

**The two float powers.** `powf(x, 7)` and `powf(x, 2)` are the correctly
rounded values, as on CUDA: glibc's are not on 0.07 % and 0.16 % of the
arguments.

**No reduction on the device.** The kernel stores the float of every pixel at
its raster position, the plane is read back, and the host adds it into one
double in `extract()`'s order and applies `extract()`'s score expression.

**No call in the kernel.** The per-pixel function is flattened into the
kernel (`__attribute__((flatten, always_inline))`) and the header's functions
are always inlined. Left as calls they took 3.4 KiB of scratch memory for
their frames, which returns wrong values on Arc A-series under xe
([ADR-1395](1395-sycl-kernels-no-scratch.md)): the first build scored 27 dB
off.

**Kernel shape.** SIMD-16 sub-groups with the default register file,
written out (`CiedeKernel`, `VmafSyclKernelShape<16, 0>`). SIMD-32 is a
quarter faster and spills 16 KiB of registers, which is scratch memory; the
256-entry register file avoids the spill and is slower than SIMD-16 at every
width.

**The gate.** `ciede` lists `sycl` in `LIBM_TWINS` at `1e-9`, the CUDA twin's
tolerance. The twin is not listed as exact: it is not bit-identical.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| fp32 pairs for every fp64 value, pair functions for the math library (this ADR) | The CUDA twin's result on a device without fp64: 1.4e-11; fp64-free; scratch-free | 50.3 ms per 4K frame instead of 16.2; a math library of our own to maintain | Chosen |
| Keep the fp32 kernel and its `5e-3` | 16.2 ms per 4K frame | 1.14e-5 from the CPU for reasons that are the twin's own | The direction is the CPU's result |
| The reference's statements in plain fp32 | Cheap; removes the `7.787 t + 16 / 116` branch, which alone was 1.12e-5 | Still fp32 where the reference is fp64: each fp32 piece alone leaves 7e-11 to 9e-7 (table in Research-1436) | Not the CPU's types |
| Replay the fp64 operations in 64-bit integers (ADR-1432's `SoftDouble`) | Bit-exact arithmetic | The math-library calls cannot be replayed (glibc's results are not specified), so the twin would still not be exact, at several times the cost | Pairs already decide all but one pixel in 1.6 million |
| Correctly rounded partial quotients and roots inside the pair functions (`ff_div()`, `sqrt_rn()`; the first version of this change) | Reuses the existing helpers | 81.5 ms per 4K frame; the same per-pixel values | The device's own division gives 50.3 ms with identical results |
| Emulated fp64 from the device compiler | The reference's source text | Arc A-series reject a kernel with an fp64 instruction | Not available |
| Per-block fp32 sums, blocks added in double (the old reduction) | No read-back of 33 MB | An order other than `extract()`'s | The sum is the CPU's or it is not |
| Change the CPU to float math | Twins could match exactly | Moves the Netflix golden values | [ADR-0024](0024-netflix-golden-preserved.md) |

## Consequences

- **Positive**: measured on an Arc A380 (xe, Level Zero, icpx 2026.0) at
  `--precision max` against a GCC build of the CPU extractor: the Netflix
  576x324 pair at 8 bits identical on 47 of 48 frames (6.9e-13 on the other);
  its 10-, 12- and 16-bit and 4:2:2 10-bit versions and both 1920x1080
  checkerboard pairs identical on every frame; BBB 3840x2160 within 1.4e-11
  on 200 frames, none identical. These are the CUDA twin's figures. Against
  the CPU extractor of the same (icx) build, which is what the gate compares:
  48 of 48 Netflix frames identical, BBB within 4.2e-12.
- **Positive**: the gate tolerance of the SYCL cell goes from `5e-3` to
  `1e-9`.
- **Positive**: per pixel on the device, three BBB 3840x2160 frames (24.9
  million pixels): every value equals the reference's fp64 statements
  evaluated with a correctly rounded `powf`. 18, 60 and 64 pixels differ from
  the GCC build's CPU extractor and 7, 30 and 8 from the icx build's, all of
  them where the host's `powf` rounds the other way.
- **Positive**: per pixel, against the same statements in fp64 on the host,
  the pair evaluation differs on 5 of 8 million pixels of near-identical
  colour pairs and on none of 8 million independent ones
  (`test_sycl_ciede_math`).
- **Negative**: a run of the twin alone takes 50.3 ms per 3840x2160 frame
  instead of 16.2 ms (medians of 11 alternating pairs of 50 frames, paired
  difference +33.9 ms, host load average 9 to 14; the `float_psnr` control
  read 3.95 and 3.91 ms), and 1.23 ms instead of 0.45 ms at 576x324. Where
  the 50.3 ms go, from kernels with a stage removed: 10.8 ms without any
  per-pixel work (the uploads, the read-back of one float per pixel and the
  host's 8.3 million additions); 19.0 ms for the two Lab conversions (8.2 the
  six `x^2.4`, 5.7 the six cube roots, 5.1 the linear arithmetic); 20.6 ms
  for the difference formula (6.6 the four cosines of `T`, 3.1 `R_T`, 2.5 the
  two hue angles, 8.3 the five square roots, the half-angle sine, the float
  divisions and the final expression). The CPU extractor of the GCC build
  takes 2 525 ms per 3840x2160 frame on one thread.
  `T-SYCL-CIEDE-EXACT-THROUGHPUT-2026-10-01`.
- **Negative**: 33 MB of device memory and as much pinned host memory at
  3840x2160 (one float per pixel).
- **Negative**: stored `ciede_sycl` outputs change by up to 1.14e-5.
- **Neutral / follow-ups**:
  - `sycl_ff_math.h` is a small math library: results to about 2^-44, good
    for following an fp64 computation whose results are rounded to fp32. It
    is not a substitute for fp64 where bit-identity is required and the
    reference has no math-library call (ADR-1422, ADR-1432 and ADR-1434
    replay the fp64 operations exactly for that).
  - The residual has two possible causes: the host's `powf` (as on CUDA) and
    pixels an fp32 pair does not decide. On the frames examined only the
    first occurs. The bound `1e-9` holds from 576x324 up; the parity test
    uses `1e-8` at 256x144.
  - The header mirrors `ciede.c`. A change to `get_lab_color()`,
    `ciede2000()` or the order of `extract()`'s sum changes
    `sycl_ciede_math.h` and the CUDA twin's `ciede_device.h` in the same PR.
    `test_sycl_ciede_math` compares the header with the fp64 statements on
    the host and in a kernel; `test_sycl_ciede_exact_contract.py` pins the
    design at source level.
  - `core/test/ciede_twin_parity.h` holds the parity cases for any backend;
    `test_cuda_ciede_parity.c` has the same cases inline and can move to it.
  - `ciede_hip` and `ciede_metal` keep the fp32 formulation and the `5e-3`
    tolerance (`T-GPU-CIEDE-CPU-ARITHMETIC-2026-10-01`). HIP has fp64 and
    takes the CUDA header; Metal has none and can take this one.

## References

- `req` (maintainer brief for the SYCL exactness lane, 2026-10-01): "results before speed; a twin reproduces the CPU bit for bit, tuning comes afterwards".
- [ADR-1426](1426-cuda-ciede-cpu-arithmetic.md) (the CUDA twin, the
  math-library class of the gate), [ADR-1395](1395-sycl-kernels-no-scratch.md),
  [ADR-0220](0220-sycl-fp64-fallback.md),
  [ADR-1367](1367-sycl-strict-fp-every-feature-tu.md),
  [ADR-0187](0187-ciede-vulkan.md),
  [ADR-0214](0214-gpu-parity-ci-gate.md),
  [ADR-0024](0024-netflix-golden-preserved.md).
- [Research-1436](../research/1436-sycl-ciede-fp32-pairs.md).
- `docs/state.md`: `T-GPU-CIEDE-CPU-ARITHMETIC-2026-10-01` (the SYCL part
  closed by this decision), `T-SYCL-CIEDE-EXACT-THROUGHPUT-2026-10-01`.
