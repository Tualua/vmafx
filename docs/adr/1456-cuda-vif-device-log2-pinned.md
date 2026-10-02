<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1456: `vif_cuda` keeps its device `log2f()`, proven equal to the CPU's log2 table on every entry, and is declared an exact twin

- **Status**: Superseded by [ADR-1462](1462-cuda-vif-reads-host-log2-table.md)
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `cuda`, `vif`, `gpu-parity`, `numerics`, `testing`, `ci`, `rc3`, `fork-local`

## Context

The fixed-point `vif` extractor (`integer_vif.c`) is integer arithmetic up to
its last step, except for one or two logarithms per pixel. The CPU reads them
from a table of 32768 `uint16_t` entries that `init()` fills with the host
math library, `roundf(log2f(32768 + i) * 2048)`
(`vif_log2_table_generate()`). `vif_cuda` evaluates the same expression per
pixel on the device (`log_generate()` in
`core/src/feature/cuda/integer_vif/vif_statistics.cuh`).

A device's `log2f()` need not return the host's bits.
[ADR-1435](1435-hip-vif-cpu-log2-table.md) found that on an AMD gfx1036: 77
table entries came out one lower and `vif_hip` matched the CPU on 49 of 440
scores. It left the CUDA twin with the sentence "It measured equal to the CPU
on an RTX 4090; whether it is on every argument has not been probed." Equal
scores on a set of frames do not settle that: a frame reaches the entries its
variances select.

Probed on an RTX 4090 (CUDA 13.4, the fatbin's own flags) against the table of
a glibc 2.44 host, for all 32768 arguments of the table's domain:

| Quantity | Count |
|---|---|
| Arguments where the device's `log2f()` differs from the host's, by one ulp | 307 |
| Arguments where `log2f(x) * 2048` is exactly `k + 0.5` (a tie) | 80 |
| Table entries that differ | 0 |

None of the 307 differences lies next to a rounding boundary, and both sides
round a tie away from zero (`roundf`). The scores agree as well: every `vif`
output of every frame equals `--backend cpu` at `--precision max` on 348
frames from 40x40 to 3840x2160 at 8 to 16 bits, with `debug=true`,
`vif_enhn_gain_limit=1.0` and `vif_skip_scale0` too.

## Decision

We will keep the device evaluation and prove it. A probe kernel,
`vif_log2_table_probe` in `integer_vif/vif_log2_probe.cu`, writes
`log_generate()` for every argument of the table's domain. It is a module of
its own, built by the rule and with the flags that build `filter1d.cu`, and
the extractor never loads it. `test_cuda_vif_log2_table` launches it and
compares all 32768 entries with `vif_log2_table_generate()`.
`test_cuda_vif_log2_contract.py` holds the statistic to taking every
logarithm from `log_generate()`, `log_generate()` to the table's expression,
and the probe to calling that same function under the same build rule.

With every entry equal, the statistic adds the integers the CPU adds on any
frame, and `scripts/ci/exact_twins.d/vif.cuda` declares the twin exact. If the
device test ever fails, the twin reads the host table on the device as
`vif_hip` does; the listing does not get a tolerance.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Keep the device `log2f()` and pin every entry (this ADR) | No change to kernels that return the CPU's values; the proof covers the whole domain, not a set of frames; a host or CUDA release on which it stops holding fails a fast test with the entries named | The equality is a property of this device library and the host's `log2f()`, not of the code; another host math library can fail the test | Chosen: the maintainer's rule for this question was to change the kernel only if an entry differs |
| Upload the CPU's table, as `vif_hip`, `vif_sycl` and `vif_metal` do | The CPU's values by construction on every host; one design on every backend; three 16-bit loads per pixel instead of three `log2f()` evaluations | Changes the upstream NVIDIA kernels and their argument lists for no difference in output today; 64 KB more device memory | Not needed while every entry is equal; it is the stated fallback |
| Declare the twin exact on the measured frames alone | Nothing to add | An entry no fixture reaches could differ; ADR-1437's rule is construction and measurement, not measurement alone | The probe is cheap and closes the gap |
| A static copy of the device's 32768 values in the test | No device needed | A third copy of the table, right for one CUDA release | The device is the thing under test |

## Consequences

- **Positive**: the parity gate compares the CPU and CUDA `vif` cells with
  tolerance 0. Measured on an RTX 4090 at `--precision max`: 1392 of 1392
  scores on 348 frames (Netflix 576x324 at 8, 10, 12 and 16 bits and as
  10-bit 4:2:2, both 1080p checkerboards, Sparks at 10 bits, 200 frames of
  BBB 3840x2160, full-range noise at 8 to 16 bits and at 40x40 to 64x64, a
  bright 16-bit 1080p pair), and 1357 values of 59 frames under `debug=true`,
  `vif_enhn_gain_limit=1.0` and `vif_skip_scale0`.
- **Positive**: the test can fail. With `log_generate()` changed to round to
  even, 41 entries differ, the test names them, and the twin matches the CPU
  on 23 of 192 scores of the Netflix pair (up to 4.2e-7 off).
- **Negative**: the pin depends on the host. The CPU's table is whatever the
  host's `log2f()` returns; on a host whose math library rounds one of the
  307 arguments differently, or with a CUDA release whose device `log2f()`
  changes, an entry can move and the test fails. That is the signal to read
  the host table on the device.
- **Neutral / follow-ups**:
  - No scoring kernel changes, so no score and no frame time changes. The
    library gains one small fatbin (`vif_log2_probe_ptx`), 22 instead of 21.
  - The probe is not a kernel of `filter1d.cu` because that file holds four
    upstream kernels above the HISS-04 function-size limit, which the
    baseline exempts only while the file is untouched. `log_generate()` is
    one inline function of one header and both modules take one flag list,
    so its value is the same in either.
  - `vif_log2_table.h` and `core/src/feature/AGENTS.md` said that no twin
    computes the table on its device; both now state the rule as "read the
    table or prove every entry".

## References

- `req` (coordinator brief for the CUDA lane, 2026-10-02): "`vif_cuda` evaluates `log2f` on the device: the HIP twin was wrong there (ADR-1435 reads the CPU's table via `vif_log2_table_generate()`); \"measured equal\" is not enough, probe it per argument over the table's whole domain and switch to the table if a single entry differs."
- `req` (same lane, resume message, 2026-10-02): "probe the device `log2f` per argument over the table's whole domain; switch to `vif_log2_table_generate()` if one entry differs; if all equal, a test that pins that and no kernel change".
- [ADR-1435](1435-hip-vif-cpu-log2-table.md),
  [ADR-1437](1437-hip-exact-twins-declared.md),
  [ADR-0500](0500-vif-perf-lut-shrink-and-filter-cache.md),
  [ADR-1403](1403-cuda-strict-fp-every-kernel.md),
  [ADR-1428](1428-exact-twins-fragments.md),
  [ADR-0214](0214-gpu-parity-ci-gate.md).
- `docs/state.md`: `T-CUDA-VIF-DEVICE-LOG2-UNPROBED-2026-10-02` (opened and
  closed by this decision).
