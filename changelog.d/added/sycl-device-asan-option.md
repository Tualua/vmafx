- **`-Dsycl_device_asan=true` instruments the SYCL backend with the DPC++ device AddressSanitizer.** The option puts the
  flags on every SYCL compile and the link (`-Dcpp_args` never reaches them, so a build configured that way
  instruments no kernel and still prints the sanitizer banner). `scripts/dev/sycl_device_asan_check.sh` (meson test
  `test_sycl_device_asan_probe`) runs a probe kernel on the device and checks what the sanitizer reports and what it
  cannot see. On oneAPI 2026.0 with an Arc A380 an instrumented libvmaf is not a usable check: a pointer held in a
  struct captured by value reads as null. See [the device sanitizer page](docs/backends/sycl/device-sanitizer.md).
