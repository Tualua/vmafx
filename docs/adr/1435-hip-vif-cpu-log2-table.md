<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1435: `vif_hip` reads the CPU's log2 table instead of evaluating `log2f()` on the device, and returns the CPU's scores bit for bit

- **Status**: Accepted
- **Date**: 2026-10-01
- **Deciders**: lusoris
- **Tags**: `hip`, `vif`, `gpu-parity`, `numerics`, `testing`, `ci`, `rc3`, `fork-local`

## Context

The fixed-point `vif` extractor (`integer_vif.c`) is integer arithmetic up to
its last step. Per scale it filters in integers, forms three variances, and
adds one or two logarithms per pixel into int64 accumulators; only
`vif_store_residuals()` leaves the integers, with two `float` values per scale
and a single-precision ratio. A GPU twin that accumulates the same integers
returns the same scores.

`vif_hip` did not. Measured on a gfx1036 (ROCm 7.2.4) at `--precision max`
against the CPU extractor on `origin/master` 80c5a0332:

| Fixture | Frames | Identical on scale 0 / 1 / 2 / 3 | Max abs diff |
|---|---|---|---|
| Netflix 576x324, 8 bit | 48 | 4 / 0 / 1 / 1 | 5.4e-7 |
| Checkerboard 1 px, 1920x1080 | 3 | 2 / 3 / 2 / 3 | 3.0e-8 |
| Checkerboard 10 px, 1920x1080 | 3 | 3 / 3 / 3 / 3 | 0 |
| Netflix 576x324, 10 bit | 3 | 1 / 0 / 0 / 0 | 3.6e-7 |
| Sparks 480x270, 10 bit | 5 | 0 / 0 / 3 / 1 | 3.6e-7 |
| BBB 3840x2160 | 48 | 5 / 3 / 5 / 3 | 3.0e-7 |

49 of 440 scores were the CPU's. Three runs of the twin gave the same values
each time, so this is arithmetic, not the device's lost stream commands
(`T-HIP-GFX1036-DROPPED-DISPATCHES-2026-10-01`).

The host tail was already the CPU's (float sums, single-precision ratio), the
filters and the variances are integers, and the gain is computed in fp64 on
the device as on the CPU. What differed was the logarithm. The CPU reads it
from a table of 32768 `uint16_t` entries that `init()` fills with the host
math library, `round(log2f(32768 + i) * 2048)`. The kernel computed
`__float2int_rn(log2f((float)v) * 2048.0f)` per pixel on the device. A probe
on the gfx1036 compared the two for all 32768 arguments:

- the device's `log2f()` differs from glibc's in 15964 of them, by one ulp;
- the product is exactly `k + 0.5` for 80 arguments, where `roundf()` rounds
  away from zero and `__float2int_rn()` to even;
- 77 table entries end up one lower on the device (36 with `roundf()` on the
  device, so both causes contribute).

Each pixel in the logarithm branch reads three entries, so nearly every frame
had a numerator or denominator off by a few 2048ths.

## Decision

We will make `vif_hip` read the CPU's table. `vif_log2_table_generate()` in
`integer_vif.h` becomes the one definition of the table: the CPU extractor
fills its state with it, `integer_vif_hip.c` builds the same 32768 values at
`init()` and uploads them to a device buffer, and the horizontal kernels of
`vif_statistics.hip` take that buffer as an argument and look every logarithm
up (`log2_lookup()`, masked as `log2_32()` / `log2_64()` mask). The kernel
file evaluates no logarithm. `vif`: `hip` is declared an exact twin, so the
parity gate compares the CPU and HIP cells with tolerance 0 at
`--precision max`.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Upload the table the CPU builds (this ADR) | The CPU's values by construction, whatever math library the host has; one definition of the table; three 16-bit loads per pixel instead of three `log2f()` evaluations | 64 KB more device memory and one more kernel argument | Chosen |
| Keep the device `log2f()` and switch to `roundf()` | A one-line change | Fixes the 80 ties only; the one-ulp differences of the device's `log2f()` still move 36 entries | Not exact |
| Evaluate the logarithm in fp64 on the device and round to float | No table. On this host it gives the CPU's 32768 entries: glibc's `log2f()` is not the correctly rounded value for 5 of the arguments, and none of the 5 moves an entry | Three fp64 logarithms per pixel; and the CPU's table is whatever the host's `log2f()` returns, so the agreement holds for this glibc and is not a property of the code | Exact by coincidence of the host library |
| Port glibc's `log2f()` to the kernel | No table, no memory reads | Ties the twin to one libc's algorithm and table; a macOS or Windows host has another | The CPU's values are defined by the host, so they have to come from the host |
| A static table in the kernel source | No upload | A third copy of the values, generated on one machine and wrong on a host whose `log2f()` differs | The same defect in another place |
| Append the table to the filter-table buffer | No new kernel argument | A buffer named for the filter taps that also holds logarithms, and an offset both sides must agree on | An explicit argument is clearer and costs nothing per frame |

## Consequences

- **Positive**: measured on a gfx1036 at `--precision max`, every score of
  every frame equals `--backend cpu`: 440 of 440 on the six fixtures above
  (49 before). With `debug=true` the frame ratio and the per-scale numerator
  and denominator sums are equal too, and so are the scores at 12 and 16
  bits, in 4:2:2, with `vif_enhn_gain_limit=1.0` and with `vif_skip_scale0`
  (see [the HIP backend page](../backends/hip/overview.md#vif_hip-returns-the-cpus-scores-bit-for-bit-2026-10-01)).
- **Positive**: the table has one definition. `integer_vif.c::log_generate()`
  is gone; the CPU extractor and the HIP host call
  `vif_log2_table_generate()`.
- **Positive**: a frame takes no longer: 44.4 and 42.7 ms at 1920x1080,
  188.2 and 163.4 ms at 3840x2160, before and after (medians of 21
  interleaved pairs of runs under other lanes' load; the samples overlap). A
  lookup replaces each `log2f()`, and a pixel in the low-variance branch no
  longer computes the fp64 gain it does not use.
- **Negative**: stored `vif_hip` scores change by up to 5.4e-7.
- **Neutral / follow-ups**:
  - `integer_vif_sycl.cpp` and `integer_vif_metal.mm` build the table with
    copies of the expression, and `test_integer_vif_log2.c` has a fourth.
    They compute on the host and are correct; they should call
    `vif_log2_table_generate()` (other lanes own those twins).
  - `vif_cuda` still evaluates `log2f()` on the device. It measured equal to
    the CPU on an RTX 4090; whether it is on every argument has not been
    probed.
  - Guards: `test_hip_vif_parity` and `_large` (seven cases, two frames each,
    `==` on every output; the first case fails on the old twin) and
    `test_hip_vif_log2_table_contract.py` (five planted regressions, no
    device).

## References

- `req` (maintainer brief for the second HIP lane, 2026-10-01): "Every HIP twin returns the CPU extractor's bits, or differs only by the math library with a derived bound." and "Known start: `vif_hip` is 1.2e-7 off the CPU on a 4K frame (seen in #1749's test). Open its state row in your PR; it has none."
- [ADR-1421](1421-rc3-rc8-candidate-map.md) (RC3: twin exactness),
  [ADR-1397](1397-psnr-hvs-twins-cpu-float-sum.md) (the exact cell),
  [ADR-0500](0500-vif-perf-lut-shrink-and-filter-cache.md) (the 32768-entry table),
  [ADR-0537](0537-hip-integer-vif-kernel-fix.md) (device copies of host
  tables), [ADR-1407](1407-hip-strict-fp-every-kernel.md),
  [ADR-0214](0214-gpu-parity-ci-gate.md).
- `docs/state.md`: `T-HIP-VIF-DEVICE-LOG2-2026-10-01` (opened and closed by
  this decision).
