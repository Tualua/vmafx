- **The x86 float ADM kernels are at the lint and HISS standard (ADR-1142).**
  `core/src/feature/x86/float_adm_avx2.c` and `float_adm_avx512.c` held the
  wavelet as one function of 118 and 196 lines; `float_adm_dwt2_avx2()` and
  `float_adm_dwt2_avx512()` keep their names and signatures and now call one
  helper for the vertical pass of a row and one for the horizontal pass.
  clang-tidy reports nothing for the two files on the cpu, cuda, hip and sycl
  lanes (7 and 26 before on each); the HISS baseline loses its two rows (260
  to 258). No score changes: no dispatch table calls these kernels, and an
  old-against-new comparison of every exported function returns the same bits
  on 71,840 inputs.
