---
paths:
  - core/src/feature/x86/convolve_avx2.c
  - core/src/feature/x86/convolve_avx512.c
  - core/src/feature/iqa/convolve.c
  - core/src/feature/common/convolution_avx.c
invariant: Reserved-identifier hygiene: no leading-underscore names.
---
<!-- markdownlint-disable MD013 -->
# IQA Convolution and Reserved Identifier Hygiene

- **Reserved-identifier hygiene** (ADR-0148): no leading-underscore
  names. IQA tree underwent sweeping `_iqa_*` →
  `iqa_*` / `_kernel` → `iqa_kernel` / `_ssim_int` →
  `ssim_int` rename; never reintroduce old spellings on
  rebase.

| Group | TUs that move in lockstep |
| --- | --- |
| **IQA convolve** (ADR-0138 + ADR-0143) | `convolve_avx2.c` + `convolve_avx512.c` + `../arm64/convolve_neon.c` + scalar `../iqa/convolve.c` + shared scanline helpers `../common/convolution_avx.c` |

- [ADR-0138](../../../../../docs/adr/0138-iqa-convolve-avx2-bitexact-double.md) —
  `iqa_convolve` widen-then-add bit-exactness.
- ADR-0143
  ([`0143-port-netflix-f3a628b4-generalized-avx-convolve.md`](../../../../../docs/adr/0143-port-netflix-f3a628b4-generalized-avx-convolve.md))
  — generalised AVX convolve scanlines.
- [ADR-0148](../../../../../docs/adr/0148-iqa-rename-and-cleanup.md) —
  reserved-identifier rename.
