<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1458: `float_adm_hip` runs the CPU's arithmetic from the header the CUDA twin runs and returns the CPU's scores bit for bit

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `hip`, `cuda`, `gpu-parity`, `numerics`, `float-adm`, `testing`, `ci`, `rc3`, `fork-local`

## Context

[ADR-1420](1420-cuda-float-adm-cpu-arithmetic.md) found the eight constructs
that put a `float_adm` twin off the CPU extractor and rewrote
`float_adm_cuda` without them; [ADR-1442](1442-float-adm-reference-divides.md)
made the reference divide, so that its scores no longer depend on the
processor and a twin needs a correctly rounded fp32 division and nothing
else. `float_adm_hip` still had every one of those constructs
(`T-HIP-FLOAT-ADM-NOT-CPU-ARITHMETIC-2026-10-01`): the angle threshold
associated as `cos^2 * (|o|^2 * |t|^2)`, rows reduced in strided partial sums
and a wave tree and added in `double` on the host, a copy of
`dwt_quant_step()` with fp32 intermediates, fp32 `1/30` and `1/15`, the
threshold's centre taps added last, an fp32 gain limit, a frame-sum floor of
`1e-2`. It also had no frame-size check and no `adm_skip_aim_scale` option.

Measured on a gfx1036 (ROCm 7.2.4) at `--precision max` against
`--backend cpu` (the dividing reference), over the seven scores of every
frame of 110 frames of typical content and 68 frames that stress the
arithmetic: 224 of 1246 values identical, the largest difference 1.3e-5
(`adm_scale0` on BBB 3840x2160).

## Decision

We will make `float_adm_hip` return the CPU extractor's values bit for bit,
with the arithmetic of the CUDA twin and no second copy of it.

**One header.** The arithmetic and the kernel argument blocks of
`core/src/feature/cuda/float_adm/float_adm_device.h` move, unchanged, to the
backend-neutral `core/src/feature/float_adm_gpu_common.h`. Every operation
that rounds goes through a macro whose default is the plain C operator. The
CUDA header keeps the `__fmul_rn()` family for device code and includes the
shared header, so no other CUDA source changes.

**The HIP spelling is the plain operators.**
`core/src/feature/hip/float_adm/float_adm_hip_math.h` defines only the
linkage, the bit casts and the power (below). On HIP the `__fmul_rn()` family
is a plain operator unless `OCML_BASIC_ROUNDED_OPERATIONS` is defined, so it
would say nothing; what makes the plain operators the reference's is the
strict FP list every HIP kernel is built with
([ADR-1407](1407-hip-strict-fp-every-kernel.md)): no contraction, and an fp32
`/` that is the correctly rounded quotient, which is what `DIVS()` is since
ADR-1442. That this holds on the device is not taken from the flag's name:
`test_hip_float_adm_math` runs the header's functions for a million samples
per run (the default options, a gain limit of 1.2, `adm_p_norm = 1`) in a
kernel built like the extractor's and on the host and demands the same bits
(every quotient of the decouple, the fp64 gain and the fp64 CSF constants
included). With `/` replaced by a reciprocal multiply the first
differing sample is the 17th.

**The kernels and the host are the CUDA twin's.** Five launches per scale
(DWT vertical and horizontal, decouple with both CSFs, the nine terms per
sample of the reduced region, one fp32 sum per row and slot), one readback of
the row sums. The host adds the rows top to bottom in fp32, takes the CSF
weights, the reduced region and the angle threshold from `adm_tools.c`
(`adm_float_reference.h`), pools with `adm_pool_bands_s()`, floors the frame
sums at the reference's `1e-10`, and calls `adm_frame_size_check()` first in
`init`, before it claims a device resource. The option
`adm_skip_aim_scale` is added with the reference's meaning.

**`adm_p_norm = 1` through the identity.** At `adm_p_norm` other than 3 both
sides raise each term with `powf()`. The gfx1036's `powf(x, 1.0f)` is not `x`
for every `x` (5.8e-13 on `aim` at 960x540), the C library's is. The HIP
spelling returns `x` itself for an exponent of 1 (`FADM_POWF`), so that
option is exact; any other exponent except 3 stays close, not equal.

**Gate.** `scripts/ci/exact_twins.d/float_adm.hip` declares the twin exact
([ADR-1428](1428-exact-twins-fragments.md)).

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| The CUDA twin's arithmetic from a shared header (this ADR) | One implementation of the decouple, the CSF, the threshold and the terms for both twins; the device-free replay against `adm_tools.c` covers both | The CUDA header is touched (it becomes the spelling) | Chosen |
| A HIP copy of `float_adm_device.h` | No CUDA file touched | Two copies of the arithmetic that drifted apart once already | HISS-19 |
| Map the macros to `__fmul_rn()` and `__fdiv_rn()` on HIP as on CUDA | Reads like the CUDA twin | On HIP those are plain operators unless a macro is defined; the spelling would claim a guarantee the build flags give | ADR-1407 |
| Trust the flag for the division and test only scores | No probe kernel | A wrong quotient would show as a score difference with no hint of the cause | The per-value test names the operation |
| The SYCL twin's evaluation without fp64 (ADR-1434) | Would also run on a device without fp64 | Soft fp64 in integers for three expressions; this device has fp64 and its product and sum are the IEEE ones | Not needed |
| Leave `adm_p_norm = 1` to the device's `powf()` | No override macro | 5.8e-13 off on one option the CPU test set covers as exact for SYCL | One conditional |

## Consequences

- **Positive**: measured on a gfx1036 at `--precision max`, every score of
  every frame equals `--backend cpu`: 1246 of 1246 values on the fourteen
  fixtures (224 before), 3204 of 3204 with `debug=true`, and 1246 of 1246
  each with `adm_enhn_gain_limit=1.2`, `adm_bypass_cm=1`,
  `adm_skip_aim_scale=1`, `adm_norm_view_dist=1.5` and `adm_p_norm=1`.
- **Positive**: no cost. Steady state inside one process, medians of nine
  interleaved pairs of runs on a loaded host: 13.4 ms before and 13.2 ms
  after per 1920x1080 frame, 80.8 and 68.4 ms per 3840x2160 frame. The twin
  launches 20 kernels per frame instead of 24 and clears nothing.
- **Positive**: frames below 17x17 are refused with the CPU's error instead
  of being scored from one-sample bands.
- **Negative**: 9 floats per sample of the reduced region of scale 0 on the
  device (48 MB at 3840x2160), as on CUDA.
- **Negative**: stored `float_adm_hip` outputs change by up to 1.3e-5.
- **Neutral / follow-ups**:
  - `adm_p_norm` other than 1 or 3: within 1.5e-7 of the CPU at 2 (911 of
    1246 values identical), because the two sides call different `powf()`.
  - The five kernels exist in `float_adm_score.cu` and in
    `float_adm_score.hip`; only their arithmetic is shared. They are 300
    lines that move samples between buffers and the shared functions.
  - `float_adm_metal` is the last twin with the old constructs
    (`T-GPU-FLOAT-ADM-CPU-ARITHMETIC-2026-10-01`).
  - Guards: `test_hip_float_adm_parity` and `_large` (the cases of
    `float_adm_twin_parity.h` the CUDA options cover, `==` on every output),
    `test_hip_float_adm_math` (device against host, value by value),
    `test_hip_float_adm_exact_contract.py` (eleven planted regressions, no
    device), `test_float_adm_device_math` (the shared header against
    `adm_tools.c`, no device) and `test_float_adm_divides_contract.py`.

## References

- `req` (coordinator, 2026-10-02): "`float_adm_hip`: the reference PR is #1781, merged as `b8f8e5a6f` (ADR-1442): the CPU divides (`DIVS(n, d) = n / d`), no reciprocal estimate, no probe. Port `float_adm_cuda`'s arithmetic (ADR-1420 as amended by ADR-1442; the SYCL port is #1787, ADR-1434, a second worked example) to HIP: fp32 `/` on the device (confirm hipcc does not rewrite it into a reciprocal multiply: per-value check against the host as SYCL's `test_sycl_float_adm_math` does), 17x17 frame check first in init (`adm_frame_size_check`), tolerance 0 at `--precision max` incl. `debug=true` outputs and the option sets the CUDA test covers, fragment `scripts/ci/exact_twins.d/float_adm.hip`."
- [ADR-1420](1420-cuda-float-adm-cpu-arithmetic.md),
  [ADR-1442](1442-float-adm-reference-divides.md),
  [ADR-1434](1434-sycl-float-adm-cpu-arithmetic.md),
  [ADR-1407](1407-hip-strict-fp-every-kernel.md),
  [ADR-1428](1428-exact-twins-fragments.md),
  [ADR-1421](1421-rc3-rc8-candidate-map.md),
  [ADR-0214](0214-gpu-parity-ci-gate.md),
  [ADR-0024](0024-netflix-golden-preserved.md).
- [Research-1420](../research/1420-cuda-float-adm-cpu-arithmetic.md),
  [Research-1437](../research/1437-hip-twin-exactness-sweep.md).
- `docs/state.md`: `T-HIP-FLOAT-ADM-NOT-CPU-ARITHMETIC-2026-10-01`,
  `T-GPU-FLOAT-ADM-CPU-ARITHMETIC-2026-10-01`.
