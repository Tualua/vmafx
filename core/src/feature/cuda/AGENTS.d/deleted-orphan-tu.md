---
paths:
  - core/src/feature/cuda/integer_ssim_cuda.c
invariant: Do not re-add deleted orphan translation units without consulting ADR-0546.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# Deleted orphan TU (ADR-0546)

`float_ssim_cuda.c` removed by ADR-0546 (`chore/hip-cuda-orphan-tu-cleanup`,
2026-05-18). Defined `vmaf_fex_float_ssim_cuda` but not listed in
`core/src/meson.build`; `integer_ssim_cuda.c` (which is compiled) also
defines same symbol and = current canonical TU. `enable_chroma` stays as
accepted, ignored option (ADR-1373, HISS-14: was public; CPU `float_ssim` has
none; kernel reads luma only). Do not re-add `float_ssim_cuda.c` without
consulting ADR-0546.
