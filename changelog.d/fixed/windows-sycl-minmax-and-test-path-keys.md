- **Windows MSVC+SYCL compiles the exact-arithmetic SYCL headers again, and two
  contract tests pass on Windows.** A `max()` macro from `<windows.h>` broke
  `std::numeric_limits<float>::max()` in a SYCL header, and two Python tests
  looked up backslash path keys with forward-slash names.
