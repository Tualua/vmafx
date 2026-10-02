- **The NEON float ADM kernels are at the lint and HISS standard (ADR-1142).**
  `core/src/feature/arm64/float_adm_dwt2_neon.c` held the wavelet as one
  function of 136 lines; `float_adm_dwt2_neon()` keeps its name and signature
  and now calls one helper for the vertical pass of a row and one for the
  horizontal pass. clang-tidy reports nothing for it and for
  `float_adm_neon.c` on the arm64 lane (6 and 5 before); the HISS baseline
  loses its row (247 to 246). No score changes: every recorded `adm` and
  `float_adm` output and model score is identical on aarch64 for scalar and
  NEON dispatch. `adm_avx2.c` and `adm_avx512.c` gain their SPDX line, and a
  suppression in `adm_tools.c` that no longer suppressed anything is removed.
