- The x86 AVX2 level now requires FMA (CPUID leaf 1 ECX bit 12) as well as AVX2,
  BMI1 and BMI2. Two AVX2 kernels are built with `-mfma`, so a virtual machine or
  emulator that reports AVX2 and masks FMA used to fault on them; it now runs the
  SSE paths with the same scores. `docs/backends/x86/avx512.md` lists what each
  level requires.
