- **`core/src/feature/adm.c` is at the lint and HISS standard (ADR-1142).**
  `compute_adm()`, the float ADM driver, was one function of 260 lines with
  eight `goto`s; it keeps its name and signature and is now a 59-line function
  over helpers for the buffers, the wavelet of a scale, a scale's three sums
  and the accumulation over the four scales. clang-tidy reports nothing for
  the file on the cpu, cuda, hip, sycl and arm64 lanes (31 before on each);
  the HISS baseline loses its nine rows (260 to 251). No score changes: every
  recorded `adm` and `float_adm` output and model score is identical on x86
  (scalar, AVX2, AVX-512) and on aarch64 (scalar, NEON). The stale tidy
  baseline entries of `float_adm.c` on the hip, sycl and arm64 lanes are
  removed; the file already measured 0.
