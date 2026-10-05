<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1806: Run Metal kernel files on the host through a Metal Shading Language shim in tests

- **Status**: Accepted
- **Date**: 2026-10-05
- **Deciders**: Lusoris
- **Tags**: metal, gpu, parity, testing, fork-local

## Context

No Apple device runs anything on the development and CI hosts. A Metal twin
is compiled by the hosted macOS runner and measured only by an outside
tester's report (ADR-1496). ADR-1498 moved the per-sample arithmetic of the
twins into headers written on `metal_portable.h` that host tests compile and
hold against the CPU, but the rest of a kernel file (indexing, reductions,
the layout of what it writes) and the host code that sizes, binds and reads
its buffers stayed unmeasured until a report came back.

The first M4 Pro report for `integer_adm_metal` (issue #2118) failed every
exact case of `test_metal_integer_adm_parity` and every `adm` cell of the
parity gate. Six defects were the cause, none of them in the shared
arithmetic header: the reduction slots were written twice as far apart as the
host read them, scale 1 read the int16 band of scale 0 through an int32
pointer, the scales-1-3 masking terms and the denominator used rounding terms
of their own, scale 0 contributed an AIM numerator under `adm_skip_scale0`,
and the noise floor multiplied in float (state row
`T-METAL-INTEGER-ADM-TWIN-DEFECTS-2026-10-05`). A transcription of the
kernel and its host into C++ reproduced the report on Linux, down to the
17th digit of one gate cell, so a host replay can see this class of defect.

## Decision

A test may compile an unmodified `.metal` kernel file on the host through
`core/test/metal_msl_host_shim.h`, reached through
`core/test/metal_msl_host/metal_stdlib` on the test's include path. The shim
removes the address spaces and `kernel`, ignores the `[[...]]` attributes,
spells the Metal scalar and vector types the kernels use, and turns
threadgroup atomics into plain loads and stores with a no-op barrier. The
test runs every thread position of each dispatch and every threadgroup as
one thread, on buffers whose sizes and element types come from the twin's
own host code, and compares the twin's scores with the CPU extractor with
`==`. For this to measure the twin and not a copy of it, the host logic that
does not touch the Metal API moves into a plain C file the Objective-C++
dispatch and the test share (`integer_adm_metal_host.c` for the first user,
`test_metal_integer_adm_host_replay`). The shim is test-only, the test
builds on LP64 hosts only (Metal's `long` is 64 bits) and does not replace
the device run of the parity test.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Shim and host replay of the unmodified kernel file (chosen) | Runs the code that ships; fails on every defect class of the #2118 report; no device, seconds on any LP64 host | One thread per threadgroup, so no barrier, race or lane-count defect is seen; not the Metal compiler; needs the host logic in a shared C file | — |
| Move every kernel body into `metal_portable.h` headers (ADR-1498 pattern only) | Value-by-value tests of each function; no shim | The defects were in indexing, binding, sizes and host glue, which a per-sample header does not hold; a large rewrite of every kernel | Covers the arithmetic only, and the arithmetic was right |
| Source-contract tests only (Python, text) | Cheap; device-free; already the pattern for every Metal port | Pins text, not behaviour: a new layout or binding mistake passes until someone writes its pattern down | Kept as a second line, not the measurement |
| A Metal-to-C translator or the Metal compiler's CPU backend | Closer to the Metal compiler | Not available on Linux; a toolchain to pin and maintain | No such tool on the hosts that run the tests |
| Wait for tester reports | Measures the real device | One report per round trip of days; every defect found late, several at a time | The #2118 round trip is what this replaces for the defects a host can see |

## Consequences

- **Positive**: a Metal twin's kernels and host logic are measured against
  the CPU on every CPU build before a tester sees them; the
  `integer_adm_metal` replay fails on each of the six defects and passes with
  their fixes. Other twins can reuse the shim.
- **Negative**: the shim's mapping is a second definition of what the
  kernels need from `<metal_stdlib>`; a kernel that uses SIMD-group
  functions, textures or threadgroup lane interplay needs the shim extended
  or stays device-only. Host code a test replays must stay in plain C.
- **Neutral / follow-ups**: the device run
  (`test_metal_integer_adm_parity`, the parity gate) stays the evidence that
  closes a Metal row; a replay pass only makes a failing report unlikely for
  the defects it can see. Windows (LLP64) does not build the replay tests.

## References

- [ADR-1496](1496-metal-gate-in-tester-bundle.md) (the tester report),
  [ADR-1498](1498-metal-twins-exact-designs.md) (the exact designs and the
  `metal_portable.h` headers), [ADR-1416](1416-cuda-adm-cpu-row-rounding.md)
  (the CUDA twin's conclusion through the CPU's helpers, which the Metal host
  now follows), [ADR-0155](0155-adm-i4-rounding-deferred-netflix-955.md)
  (the INT32_MIN rounding term).
- Issue #2118: M4 Pro (macOS 26.6) tester report.
- Source: the RC3 coordinator's brief for this row (paraphrased): compile the
  unmodified kernel through a shim, drive it with the twin's buffer sizes and
  types, compare with the CPU extractor, keep it Metal-test-only, and record
  its limits and the alternatives in an ADR.
