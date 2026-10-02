<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1462: `vif_cuda` reads the CPU's log2 table instead of evaluating `log2f()` on the device

- **Status**: Accepted, Supersedes [ADR-1456](1456-cuda-vif-device-log2-pinned.md)
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `cuda`, `vif`, `gpu-parity`, `numerics`, `testing`, `ci`, `rc3`, `fork-local`

## Context

The fixed-point `vif` extractor reads its logarithms from a table of 32768
`uint16_t` entries that the host math library fills
(`vif_log2_table_generate()`). `vif_cuda` evaluated the same expression per
pixel on the device. [ADR-1456](1456-cuda-vif-device-log2-pinned.md) probed
that for every entry on an RTX 4090 (CUDA 13.4) against a glibc 2.44 host:
the device's `log2f()` differs from the host's on 307 arguments by one ulp,
and no table entry differs. It kept the device evaluation and pinned the
equality with a probe kernel and a device test.

That equality is a property of one device library meeting one host library.
The CPU's table is whatever the host's `log2f()` returns; another libm, or a
CUDA release with another device `log2f()`, can move an entry, and the twin
then differs from the CPU until someone notices a failing test. The
maintainer decided that exactness has to hold by construction, as it does
for `vif_hip` ([ADR-1435](1435-hip-vif-cpu-log2-table.md)), `vif_sycl` and
`vif_metal`, which read the host's table.

Two constraints shape how:

- `core/src/feature/cuda/integer_vif/filter1d.cu` holds four upstream kernels
  above the HISS-04 function-size limit. The baseline exempts them while the
  file is untouched; a new kernel argument would touch it and require
  splitting those kernels.
- The CUDA loader this project uses (ffnvcodec) binds the legacy
  `cuModuleGetGlobal()` symbol, which a context created with the current API
  refuses (`CUDA_ERROR_INVALID_CONTEXT`), so the host cannot write a module
  global directly.

## Decision

We will make `vif_cuda` read the CPU's table. `vif_statistics.cuh` holds it
as the module global `vif_cuda_log2_table` and `log2_lookup()` reads every
logarithm of the statistic from it, masked as `log2_32()` / `log2_64()` mask.
No vif kernel source evaluates a logarithm.

The host fills the table once per module load, in `init_fex_cuda()` before
any buffer is allocated or frame submitted:
`vmaf_cuda_vif_upload_log2_table()` generates the table with
`vif_log2_table_generate()`, stages it in a device buffer and launches
`vif_cuda_log2_table_transfer`, a kernel of the same module that copies the
staged values into the global, and waits for it.

The table is a module global and the copy a kernel so that `filter1d.cu` is
not touched: its kernels keep their signatures, their arithmetic and their
machine code apart from the logarithm.

The probe fatbin of ADR-1456 (`vif_log2_probe.cu`) is removed; nothing uses
it. `test_cuda_vif_log2_table` becomes the regression test of the upload: it
loads the module, checks the table is empty, runs the extractor's upload,
reads the table back through the transfer kernel and compares all 32768
entries with the host's.

Removing the fatbin undoes four lines of `core/src/meson.build` and the
target count that #1810 (`6d2b9ffa6`) added. The silent-revert gate
([ADR-1291](1291-silent-revert-declared-reversals.md)) reports that, and
`scripts/ci/silent-revert-allowlist.json` declares it under this ADR: one
`reverse-hunk` entry for the build file, bound to that commit and those
lines, and one `rewind` entry for the test that holds the count.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Module global, filled by a transfer kernel of the same module (this ADR) | The CPU's values by construction on every host and CUDA release; `filter1d.cu` untouched; no change to the frame time; one extra kernel launch at init | 64 KB of module memory per extractor instance; the transfer kernel's read direction exists for the test | Chosen |
| A table pointer as a new kernel argument, as `vif_hip` | The usual shape | Touches `filter1d.cu`, which revokes the baseline's exemption for four upstream kernels of 130 to 225 lines; splitting them into helpers is a rewrite of upstream NVIDIA code for no change in what it computes | The global reaches the same result without it |
| Write the global from the host with `cuModuleGetGlobal()` | No transfer kernel | The loader binds the legacy symbol; measured: `CUDA_ERROR_INVALID_CONTEXT` | Not available through the project's loader |
| Resolve `cuModuleGetGlobal_v2` by hand from the loader's library handle | No transfer kernel | A private second path into the driver next to the loader | Not worth it for one init-time copy |
| Keep the device `log2f()` and the pin (ADR-1456) | No change | Exact for one pair of device and host library | The maintainer's decision: by construction |
| A table in `__constant__` memory | Fast reads | 64 KB is the whole constant bank; the loader problem is the same | No gain |

## Consequences

- **Positive**: `vif_cuda` adds the integers the CPU adds, whatever the host
  math library or the CUDA release. Measured on an RTX 4090 at
  `--precision max` against `--backend cpu`: 1392 of 1392 scores on 348
  frames (Netflix 576x324 at 8, 10, 12 and 16 bits and as 10-bit 4:2:2, both
  1080p checkerboards, Sparks at 10 bits, 200 frames of BBB 3840x2160, noise
  at 8 to 16 bits and at 40x40 to 64x64, a bright 16-bit 1080p pair), and
  4508 of 4508 values of 196 frames under `debug=true`,
  `vif_enhn_gain_limit=1.0` and `vif_skip_scale0`. The same as before the
  change: no score moves on this host.
- **Positive**: no measurable change in the frame time; the numbers are in
  [the CUDA backend page](../backends/cuda/overview.md#vif_cuda-returns-the-cpus-scores-bit-for-bit-2026-10-02).
- **Positive**: one kernel module fewer (21 fatbins again).
- **Negative**: 64 KB of device memory per `vif_cuda` instance for the table,
  and one more kernel launch and stream wait at init.
- **Neutral / follow-ups**:
  - `vif_log2_table.h` states the rule again as "no twin computes the table
    on its device".
  - Guards: `test_cuda_vif_log2_table` (device), `test_cuda_vif_log2_contract.py`
    (eight planted regressions, no device), `test_cuda_vif_parity`.

## References

- `req` (coordinator, 2026-10-02, on the open point of ADR-1456): "`vif_cuda` reads the host's table (my call on your open point): exactness must hold by construction, not for \"CUDA 13.4 vs glibc 2.44\". After #1810 lands, switch the scoring kernel to the uploaded `vif_log2_table_generate()` table as `vif_hip` does (ADR-1435), restore the rule text in `vif_log2_table.h` to \"no twin computes the table on its device\", keep your probe test as a regression test for the table upload (or drop the probe fatbin if nothing uses it: no dead code)".
- `req` (same message): "If touching `filter1d.cu` revokes HISS-04 baseline entries of upstream kernels, refactor the functions you touch into helpers as the touched-file rule requires; if that would change kernel arithmetic or cost, put the table read where it does not (say what you chose)."
- [ADR-1456](1456-cuda-vif-device-log2-pinned.md) (superseded),
  [ADR-1435](1435-hip-vif-cpu-log2-table.md),
  [ADR-0500](0500-vif-perf-lut-shrink-and-filter-cache.md),
  [ADR-1428](1428-exact-twins-fragments.md),
  [ADR-0214](0214-gpu-parity-ci-gate.md).
- `docs/state.md`: `T-CUDA-VIF-DEVICE-LOG2-HOST-DEPENDENT-2026-10-02` (opened
  and closed by this decision).
