- **An Intel-compiler build returns a GCC build's CPU scores.** A build with
  `icx` / `icpx` (every SYCL build, the dev image's `vmaf`) linked Intel's math
  library `libimf`: the driver turns a `-lm` into `-limf -lm`, and the
  icx-built `vmaf` carried a static copy of it that `libvmaf.so`'s calls to
  `log10`, `pow`, `powf`, `log2f`, `exp`, `log` and 24 other math functions
  bound to. `psnr`, `psnr_hvs`, `ciede`, integer `adm`, `float_adm` (with a CSF
  weight override) and the default model's `vmaf` differed from a GCC build in
  the last digits (268 of 13288 values at `--precision max`, up to 1.3e-7 in
  `vmaf`). Every icx / icpx link now gets `-no-intel-lib=libimf`, so the math
  functions come from glibc's `libm`: 13288 of 13288 values are identical, and
  the twins of a SYCL build equal a GCC build's CPU extractor as well as their
  own build's
  ([ADR-1495](docs/adr/1495-icx-system-libm.md)). `ciede` in an icx build runs
  43 % faster (95.6 to 54.3 ms per 1920x1080 frame on four threads).
  `test_icx_system_libm` reads the build's `libvmaf.so` and `vmaf` and fails if
  a math call resolves to `libimf`; it skips on GCC and clang builds. Windows
  `icx-cl` builds are not covered yet.
