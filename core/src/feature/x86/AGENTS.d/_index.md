# AGENTS.md — core/src/feature/x86

Orientation for agents working on AVX2 / AVX-512 feature SIMD
paths. Parent: [../AGENTS.md](../../AGENTS.md).

## Scope

Per-feature AVX2 + AVX-512 SIMD implementations. Every TU here mirrors
scalar reference one level up (e.g. `ssim_avx2.c` ↔ `../iqa/ssim_tools.c`,
`adm_avx2.c` ↔ `../adm.c`), dispatched at runtime from feature's
`*_dispatch.c` via `vmaf_get_cpu_flags_x86()` (see
[`../../x86/cpu.c`](../../../x86/cpu.c)).

## Ground rules

- **Every SIMD `.h` file MUST be self-contained.** Include every
  standard header naming a type used in file's own declarations
  — never rely on transitive includes from consumer `.c` files. In
  particular, any header declaring `ptrdiff_t` parameter MUST
  include `<stddef.h>` directly. Standalone-include failures on Apple
  Clang and Ubuntu ARM Clang are CI regressions (see PR #914 for
  cambi family; fixed for motion family in accompanying PR).
- **Parent rules** apply in full (see [../AGENTS.md](../../AGENTS.md) +
  [../../AGENTS.md](../../../AGENTS.md)).
- **Bit-exactness with scalar reference is non-negotiable.** Every
  AVX2 / AVX-512 kernel here mirrors scalar TU byte-for-byte under
  `FLT_EVAL_METHOD == 0`. Bit-exact regression tests in
  [`../../../test/`](../../../test/) (`test_*_simd.c`, migrated through
  [`simd_bitexact_test.h`](../../../test/simd_bitexact_test.h) harness
  per ADR-0245) catch ULP drift; pushing through them without
  paired scalar update is regression.

## Twin-update rules

These TUs come in twin-bundles. Change to one half **must** ship
with matching change to other halves in **same PR**:

Complete invariants live in [../AGENTS.md
§"Rebase-sensitive invariants"](../../AGENTS.md); this table is
**index** of which file groups move together.

## Upstream-sync notes

- Every TU in this directory carries Netflix copyright header
  (`Copyright 2016-202x Netflix, Inc.`) — these files are
  upstream-mirror at structural level even though several
  carry fork-only refactors (ADR-0146 helper splits in
  `vif_statistic_avx2.c`; ADR-0143 `static` + `ptrdiff_t` in
  `convolve_avx2.c`).
- On `/sync-upstream` or `/port-upstream-commit`: if Netflix
  patch touches any TU in this directory, walk corresponding
  twin in `../arm64/` + scalar reference + SIMD-tail
  reduction helper (`../iqa/ssim_accumulate_lane.h` for SSIM,
  `../iqa/convolve.c` for convolve) before merging. Cross-
  backend parity gate at `places=4`
  ([`scripts/ci/cross_backend_parity_gate.py`](../../../../../scripts/ci/cross_backend_parity_gate.py),
  ADR-0214) catches scalar↔SIMD drift but only after full run.
