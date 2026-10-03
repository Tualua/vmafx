<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1477: SpEED evaluates Netflix's fp64 expressions again, and its GPU twins form the entropies and the score on the host with the same statements, so every twin returns the CPU's scores bit for bit on any C library

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `numerics`, `speed`, `upstream-parity`, `cuda`, `hip`, `sycl`, `gpu-parity`, `testing`, `ci`, `rc3`, `fork-local`

## Context

The fork's port of Netflix's SpEED extractors (#213, `32f275788`, upstream
`d3647c73`) turned three fp64 expressions of `speed.c` into fp32. Netflix
master (`libvmaf/src/feature/speed.c` at `9e48141b`) has:

| Where | Netflix | The port |
|---|---|---|
| `create_givens()`, lines 418 and 423 | `float s1 = 1.0 / sqrt(1 + t * t);` | `1.0f / sqrtf(1.0f + t * t)` |
| `update_entropy()`, lines 802 and 803 | `log2(L * S[..] + sigma_nn) + log2(2 * M_PI * M_E)` | `log2f(..) + log2f(2.0f * (float)M_PI * (float)M_E)` |
| `get_speed_score()`, lines 897 to 928 | `log2(1 + variance)`, `(a + b) / 2.0`, `0.75 * a + 0.25 * b` | `log2f(1.0f + variance)`, `/ 2.0f`, `0.75f * a` |

Each Netflix statement is fp64 arithmetic rounded to `float` once, on
assignment. The fp32 forms round earlier and call another library function.
The upstream parity audit of 2026-10-02 measured the result through the C API
at `%.17g` against a GCC build of Netflix/vmaf `cea2b4d8` (its SpEED sources
are master's): `speed_chroma` differed on
212 to 229 of 261 frames (at most 2.3e-5 for `_u` and `_v`, 1.1e-5 for
`_uv`), `speed_temporal` on 190 of 320 (at most 6.6e-4), and the four
`vmaf_v1.0.16` models, whose first feature is `speed_chroma_uv` in weighting
mode 5, on 93 to 186 of 204 scores (1.6e-5 to 2.5e-5). No ADR recorded the
change; the port's commit describes a cherry-pick of upstream.

The maintainer's rule for inherited code is that it evaluates as Netflix's
source does unless an ADR says why not, and for this case that the CPU and
every twin go back to Netflix's form.

The three GPU twins made that harder than a three-line revert. Since
[ADR-1358](1358-sycl-speed-device-resident-linalg.md),
[ADR-1380](1380-cuda-speed-device-resident-pipeline.md) and
[ADR-1384](1384-hip-speed-device-resident.md) each ran the whole chain on the
device, the entropies and the score included, with a `log2` evaluated in fp32
pairs and rounded to `float`. That was the fp32 form of the port. Netflix's
form needs an fp64 logarithm of a `float` argument (and, in weighting modes 3
to 6, of an fp64 argument: the default model uses mode 5), added and
multiplied in fp64. No SpEED kernel may use fp64 (the Arc A380 has none, and
the CUDA and HIP kernel files are fp64-free by contract), and no device has
the host's `log2`: glibc's is not correctly rounded, Intel's is another
function again.

## Decision

1. **`speed.c` and `speed_internal.c` carry Netflix's three expressions.**
   The spellings of #1209 that give the same value (`sqrtf` of an fp32 norm,
   `/ 2.0f` in `trailing_eigenvalue()`) stay: `(float)sqrt((double)x)` equals
   `sqrtf(x)` for every `float`, and halving is exact.
2. **The device chain of a twin ends at the variances.** A twin reads one
   block back per frame, at collect time: per channel the two status words,
   the 25 eigenvalues and the variance of every block
   (`SpeedGpuTailLayout`, `core/src/feature/speed_gpu_common.h`). That is
   what `est_params()` holds when it reaches its entropy loop.
3. **`speed_internal_gpu_tail_scores()` forms the entropies and the score
   from that block on the host** (`core/src/feature/speed_internal.c`). Its
   statements are `speed.c`'s (`update_entropy()`, steps 8 and 9 of
   `est_params()`, `get_speed_score()`, and `speed_extract_score()`'s zero
   when exactly one side was singular), built into the same library with the
   same flags, and they call the host's `log2`. The CUDA, HIP and SYCL
   pipelines call it after their one wait. No kernel evaluates a logarithm.
4. **`speed_givens_unit()` is the rotation's fp64 statement in fp32**
   (`core/src/feature/speed_givens.h`, one definition for the three twins).
   `create_givens()` divides the smaller magnitude by the larger, so
   `u = 1 + t * t` is one of the 2^23 + 1 floats of [1, 2]. The routine
   returns `(float)(1.0 / sqrt((double)u))` from the correctly rounded fp32
   square root and reciprocal, their two residuals and one correction, and
   `core/test/test_speed_upstream_form.c` compares it with Netflix's
   statement on every one of those inputs. The same test holds `speed.c`'s
   three functions to Netflix's statements, evaluated in the test with the
   host's own `sqrt()` and `log2()`, so that check holds on every C library;
   only its comparison with the values of a Netflix build is limited to
   glibc, whose `log2()` those values were measured with. There is no tolerance and no table
   of exceptions. The fp32 form `1.0f / sqrtf(u)` differs from Netflix's on
   2,907,055 of them.
5. **The six gate cells are exact.** `scripts/ci/exact_twins.d/` gains
   `speed_chroma.{cuda,hip,sycl}` and `speed_temporal.{cuda,hip,sycl}`, and
   `LIBM_TWINS` loses both features. The twins' parity tests assert `==`.
6. **What the device no longer needs is removed**: the fp32-pair `log2` of
   each twin, `core/src/feature/speed_log2_hard_cases.h` (48 inputs), the
   score kernels (`speed_score_kernel`, `speed_hip_score`, `launch_score`),
   the `ent`, `contrib` and result buffers, and
   `core/src/feature/speed_constants.h`.

This replaces the following statements of accepted ADRs, which stay as
written:

- ADR-1358, ADR-1380, ADR-1384: "every per-frame stage runs on the device"
  and "the host reads one `SpeedGpuFrameResult` back". The stages up to the
  solved linear system run on the device; the host reads the tail block and
  forms the entropies and the score. One wait per frame, at collect time, and
  no host stage before it, still hold. Their `log2` in fp32 pairs, ADR-1380's
  table of 48 hard cases and ADR-1384's host `log2f` test seam are gone.
- ADR-1358: "bit-identical to the CPU" was true against an icx build only.
  It holds against every build now.
- [ADR-1430](1430-cuda-speed-chroma-log2f-bound.md),
  [ADR-1452](1452-hip-speed-chroma-log2f-bound.md): "the twin keeps its
  correctly rounded `log2`; the gate bounds the cell at `5e-6`". The twin
  has no `log2`, and the cell is exact.
- [ADR-1460](1460-gate-speed-temporal-and-uncovered-twins.md): the
  `speed_temporal` cells' `4e-5`. They are exact.

## Alternatives considered

| Option | Result | Verdict |
|---|---|---|
| Evaluate an fp64 `log2` on the device, correctly rounded: fp64 on CUDA and HIP, 64-bit integers on SYCL | Matches a CPU whose `log2` is correctly rounded. glibc's is not, so a glibc build keeps a difference (rarer than before, because the fp64 result is rounded to `float`, but not zero) and the cells stay `LIBM_TWINS` bounds. Proving the rounding needs the worst cases of `log2` over fp64 arguments (modes 3 to 6) and about 128 bits of working precision, three times | Rejected: more code on every backend for a cell that still is not `0` |
| Port glibc's `log2` to the device | Matches one C library and none of the others (libimf in the icx build, musl, the Windows runtime); ADR-1430 rejected the same route for `log2f` | Rejected |
| fp64 on the device for the Givens rotation where the device has it | Two implementations (fp64 on CUDA and HIP, pairs on SYCL) and the end of the fp64-free contract of two kernel files, for a statement whose fp32 form is proven on every input | Rejected |
| Revert the CPU only and widen the twins' bounds to what they then measure | `speed_chroma` 2.3e-5 and `speed_temporal` 6.6e-4 from the CPU: the fp32 form in the twins instead of the CPU | Rejected: the decision names the twins |
| Keep the fp32 form and record it as a deliberate deviation | Faster by nothing measurable on the CPU; every stored `vmaf_v1.0.16` score differs from Netflix's | Rejected by the maintainer's rule |
| **Device chain to the variances, host tail with `speed.c`'s statements** | Bit-identical on every C library, less device code, 38 to 380 microseconds of host work per frame | **Chosen** |

## Consequences

- **Positive**:
  - The CPU extractors return Netflix master's values. Harness at `%.17g`,
    scalar, default dispatch and AVX2: `speed_chroma_u`, `_v`, `_uv` 261 of
    261 frames, `speed_temporal` 320 of 320, 3564 of 3564 `speed_chroma`
    option values. What remains differs by design: `speed_max_val` on
    `speed_temporal` ([ADR-1301](1301-speed-nonfinite-score-fails-frame.md);
    Netflix ignores the option there), `speed_prescale=2.0` with `lanczos4`
    (Netflix reads past its buffers;
    [ADR-1480](1480-speed-frame-buffers-prescale-above-one.md)), and frames
    too small for SpEED, which Netflix crashes on and the fork refuses
    ([ADR-1481](1481-extractor-failure-fails-the-run.md)).
  - GCC and clang, x86-64 and aarch64 builds of the fork return the same
    bits on every probe.
  - Each twin equals the CPU extractor of its own build on every measured
    value: 759 `speed_chroma` and 256 `speed_temporal` values at the default
    options on 20 fixtures, 2052 and 342 over 18 option sets, on an RTX 4090
    and a gfx1036 against a GCC build and on an Arc A380 against an icx
    build ([Research-1477](../research/1477-speed-upstream-double-math.md)).
  - `T-CUDA-SPEED-CHROMA-GLIBC-LOG2F-2026-10-01` and
    `T-HIP-SPEED-CHROMA-GLIBC-LOG2F-2026-10-02` close: the cells are `0`.
  - The upstream parity guard
    ([ADR-1487](1487-upstream-parity-policy-and-guard.md)) loses its three
    pending SpEED fragments (`speed_chroma.float-math`,
    `speed_temporal.float-math`, `model.speed-float-math`); `make
    upstream-parity` attributes no SpEED difference to anything but the
    deliberate deviations above.
  - The three kernel files lose about 510 lines, and the three twins share
    the host tail and the Givens routine instead of three copies of a `log2`.
- **Negative**:
  - CPU scores change: `speed_chroma` by up to 2.3e-5, `speed_temporal` by
    up to 6.6e-4, `vmaf_v1.0.16_*` by up to 2.5e-5. GPU scores change with
    them. Stored scores are not comparable in those digits.
  - The host works at collect time: 38 microseconds for `speed_chroma` at
    1920x1080, 81 for `speed_temporal`, 162 to 180 and 379 at 3840x2160
    (Ryzen 9 9950X3D, glibc 2.44), and the readback is 108 bytes per channel
    plus four per block instead of 40 bytes.
  - No twin is measurably slower for it: before and after in alternation,
    15 samples each, the medians per frame differ by -0.43 to +0.04 ms on an
    RTX 4090, a gfx1036 and an Arc A380 at 1920x1080 and 3840x2160, inside
    the spread of the samples (Research-1477). What the host gained is about
    what the device lost: the fp32-pair `log2` and the score kernel. No
    tuning row is opened.
- **Neutral / follow-ups**:
  - `testdata/scores_cpu_*.json` hold no SpEED metric and do not move.
  - The Netflix golden gate passes unchanged on x86-64 and aarch64.
  - `speed.c`'s three expressions are upstream's again: an upstream sync
    takes upstream's side there.
  - A change to `update_entropy()`, `get_speed_score()` or
    `speed_extract_score()` in `speed.c` changes
    `speed_internal_gpu_tail_scores()` in the same PR; a change to
    `create_givens()` changes `speed_givens.h`.

## References

- `req` (popup answer, 2026-10-02): "Netflix's source, deviations only by ADR".
- `Q` (popup answer, 2026-10-02): "Revert CPU and all twins (Recommended)".
- Netflix/vmaf `libvmaf/src/feature/speed.c` at `9e48141b`; fork port
  `32f275788` (#213) of upstream `d3647c73`.
- [ADR-1358](1358-sycl-speed-device-resident-linalg.md),
  [ADR-1380](1380-cuda-speed-device-resident-pipeline.md),
  [ADR-1384](1384-hip-speed-device-resident.md),
  [ADR-1430](1430-cuda-speed-chroma-log2f-bound.md),
  [ADR-1452](1452-hip-speed-chroma-log2f-bound.md),
  [ADR-1460](1460-gate-speed-temporal-and-uncovered-twins.md),
  [ADR-1462](1462-cuda-vif-reads-host-log2-table.md) (host values for a
  device, the precedent), [ADR-1428](1428-exact-twins-fragments.md),
  [ADR-1459](1459-speed-cov-kernel-exact.md),
  [ADR-1461](1461-strict-fp-every-translation-unit.md).
- [Research-1477](../research/1477-speed-upstream-double-math.md).
