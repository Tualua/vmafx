---
paths:
  - core/src/meson.build
  - core/src/feature/feature_extractor.cpp
invariant: Cross-feature plumbing lives in parent directory; feature/x86 contains kernel TUs.
---
# Orientation: scope, layout, workflows

```text
feature/x86/
  <feature>_avx2.{c,h}      # AVX2 path (Haswell+ baseline)
  <feature>_avx512.{c,h}    # AVX-512 path (Skylake-X+ baseline; ICL flag for AVX512BW/VBMI2)
  ms_ssim_decimate_*.{c,h}  # 9-tap LPF SIMD (one of four byte-identical TUs — see parent AGENTS.md)
```

Cross-feature plumbing (dispatch tables, `simd_dx.h` macro
header, runtime CPUID gate) lives in `../` — this directory
contains only kernel TUs.

## Adding a new AVX2 / AVX-512 TU

Use [`/add-simd-path`](../../../../../.claude/skills/add-simd-path/SKILL.md).
Skill scaffolds:

1. TU + header, with `#pragma STDC FP_CONTRACT OFF` at
   top and appropriate `#include "../simd_dx.h"`.
2. Dispatch entry in feature's `*_dispatch.c` so
   `vmaf_get_cpu_flags_x86()` selects new path.
3. Bit-exact regression test under `../../test/test_<feature>_simd.c`
   using [`simd_bitexact_test.h`](../../../test/simd_bitexact_test.h)
   harness (ADR-0245).

## Governing ADRs

See [../AGENTS.md §Governing ADRs](../../AGENTS.md) for full list.
Ones that carve invariants on this directory specifically:

- [ADR-0245](../../../../../docs/adr/0245-simd-bitexact-test-harness.md) —
  shared bit-exact test harness.
