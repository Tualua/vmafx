---
paths:
  - core/test/simd_bitexact_test.h
  - core/test/test_ssimulacra2_simd.c
  - core/test/test_psnr_hvs_avx2.c
  - core/test/test_psnr_hvs_neon.c
invariant: SIMD parity tests check what kernel leaves outside output; use simd_bitexact_test.h and pragma clang fp contract(off).
---
<!-- markdownlint-disable MD013 -->
# SIMD parity tests and bit-exact harness

- **SIMD parity tests check what kernel leaves outside its output, not only
  what it writes inside.** Kernel storing past its region passes region-only
  comparison. Netflix/vmaf `03b5562c5` + `ea012e387` both that shape, unnoticed
  here ([Research-2063](../../../docs/research/2063-upstream-sync-2026-09-adm-vif-simd.md)).
  Allocate output planes at production stride, add trailing slack, fill with
  `simd_test_guard_fill()` from `simd_bitexact_test.h`, assert
  `SIMD_GUARD_ASSERT_UNTOUCHED(simd_test_guard_count_outside(plane, rect), ...)`
  after call. Inputs + outputs share buffer -> byte-compare whole allocation
  against scalar run instead (`test_vif_neon.c`,
  `test_integer_vif_avx2_stages.c`). Sweep small geometries both sides of each
  vector stride, down to extractor minimum of 17. ADM + VIF parity tests =
  reference.
- **SSIMULACRA 2 SIMD test scalar reference is icx-FMA-sensitive.**
  Scalar reference functions in
  [`test_ssimulacra2_simd.c`](../test_ssimulacra2_simd.c) (e.g.
  `ref_linear_rgb_to_xyb`) must match AVX2 / AVX-512 SIMD libs
  bit-for-bit, but those libs use explicit `_mm*_mul_ps` +
  `_mm*_add_ps` intrinsics (no `_mm*_fmadd_ps`). Under icx 2025.3 /
  2026.0, neither `-ffp-contract=off`, `-fp-model=precise`, nor
  `#pragma STDC FP_CONTRACT OFF` suppresses scalar FMA contraction —
  only **`#pragma clang fp contract(off)`** does. File carries
  file-scope clang FP pragma block (with `-Wunknown-pragmas`
  suppression for GCC) at top; do not remove it. Any new ref
  function added to this file inherits pragma scope automatically.
  See
  [ADR-0973](../../../docs/adr/0973-master-ci-regressions-verified-2026-05-31.md).
  **Rebase-sensitive**: if refactor moves ref functions out into
  helper header, port pragma block with them.
- **New SIMD parity test → use [`simd_bitexact_test.h`](../simd_bitexact_test.h)**
  (ADR-0245). Shared harness centralises `xorshift32` PRNG, portable
  POSIX/MinGW/MSVC aligned allocator, x86 AVX2 CPUID gate, and
  `SIMD_BITEXACT_ASSERT_MEMCMP` / `SIMD_BITEXACT_ASSERT_RELATIVE`
  assertion macros. Do not re-implement these inline.
  `#include "test.h"` must precede
  `#include "simd_bitexact_test.h"` (conventional order; previous
  double-include risk removed when `test.h` gained include guard in
  this PR). Existing migrated tests (`test_psnr_hvs_avx2.c`,
  `test_psnr_hvs_neon.c`, `test_moment_simd.c`,
  `test_motion_v2_simd.c`) are reference templates;
  `test_ssimulacra2_simd.c` is intentional non-migrated example (its
  `fill_random` FP rounding order is load-bearing for input bit
  patterns).

- [ADR-0245](../../../docs/adr/0245-simd-bitexact-test-harness.md) —
  SIMD bit-exact test harness shared header
  (`simd_bitexact_test.h`).
