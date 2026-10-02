- **`ciede2000` no longer depends on the compiler or on the C library's `powf`
  for its squares; scores of GCC-built binaries move by up to 2e-11.**
  `ciede.c` squared a `float` with `powf(x, 2)`. GCC calls the C library
  there; clang and icx replace the call by a product, which is the correctly
  rounded square, and glibc's `powf` returns the other neighbouring `float` on
  about 0.12 % of the arguments. A GCC build and a clang build therefore
  differed on 65 of 180 measured frames (the Netflix 576x324 pair at 8 to 16
  bits and as 10-bit 4:2:2, Sparks, both 1920x1080 checkerboard pairs, Big
  Buck Bunny at 1920x1080 and 3840x2160), by at most 2.0e-11. The source now
  writes the product, and the 13 `pow(x, 2)` of the formula as products too
  (their values do not change)
  ([ADR-1467](docs/adr/1467-ciede-squares-as-products.md)). x86-64 and
  aarch64 builds with GCC and with clang return the same `ciede2000` on all
  180 frames. What you see: `ciede2000` from a GCC-built binary moves on those
  65 frames by at most 2.0e-11, far below the default `%.6f`; a clang or icx
  build does not move; MSVC and macOS builds compute the same expression as
  every other build (not measured here). The CUDA, SYCL and HIP twins are
  closer to the CPU: at most 5.2e-12 from a GCC build (2.0e-11 before), and
  `ciede_cuda` itself moves by up to 1.1e-13 on 3 of 180 frames because it
  now multiplies as well. `test_ciede_device_math` runs on every
  architecture.
