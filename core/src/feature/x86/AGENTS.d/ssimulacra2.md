---
paths:
  - core/src/feature/x86/ssimulacra2_avx2.c
  - core/src/feature/x86/ssimulacra2_avx512.c
  - core/src/feature/x86/ssimulacra2_host_avx2.c
  - core/src/feature/ssimulacra2.c
invariant: SSIMULACRA 2 picture_to_linear_rgb colour matrix is unified ON FMA across all implementations.
---
# SSIMULACRA 2 SIMD Ports and FMA Unification

- **Exception: SSIMULACRA 2 `picture_to_linear_rgb` colour matrix
  is unified ON FMA across all implementations** (ADR-0891). AVX2 /
  AVX-512 main loops use `_mm256_fmadd_ps` / `_mm512_fmadd_ps`;
  scalar tails + `test_ssimulacra2_simd.c` reference
  use `fmaf()`. Reason: under icx + `-mfma`, prior explicit
  `_mm256_add_ps(_, _mm256_mul_ps(_, _))` pattern was
  auto-fused to FMA despite `-fp-model=precise`, while gcc kept
  it as separate mul+add; unifying on FMA on both sides =
  cross-compiler bit-exact pairing. Left-to-right
  associativity of `G = Yn + cb_g*Un + cr_g*Vn` preserved by
  chaining two FMAs (`G = fmaf(cb_g, Un, Yn);
  G = fmaf(cr_g, Vn, G);`). Never revert to separate mul+add
  on rebase — test fails under icx.

| Group | TUs that move in lockstep |
| --- | --- |
| **SSIMULACRA 2 SIMD** (ADR-0161 / 0162 / 0163 / 0252) | `ssimulacra2_avx2.c` + `ssimulacra2_avx512.c` + `../arm64/ssimulacra2_neon.c` + `../arm64/ssimulacra2_sve2.c` + `ssimulacra2_host_avx2.c` + `../arm64/ssimulacra2_host_neon.c` + scalar `../ssimulacra2.c` + Vulkan host-path call site `../vulkan/ssimulacra2_vulkan.c` |

- [ADR-0161](../../../../../docs/adr/0161-ssimulacra2-simd-bitexact.md) +
  [ADR-0162](../../../../../docs/adr/0162-ssimulacra2-iir-blur-simd.md) +
  [ADR-0163](../../../../../docs/adr/0163-ssimulacra2-ptlr-simd.md) +
  [ADR-0252](../../../../../docs/adr/0252-ssimulacra2-host-xyb-simd.md) —
  SSIMULACRA 2 SIMD ports.
