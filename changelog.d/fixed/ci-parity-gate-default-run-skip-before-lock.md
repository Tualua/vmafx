- **`test_cuda_parity_gate_default_run` no longer times out on a build
  without CUDA while another job holds the CUDA device lock.** The test takes
  the per-device lock before it runs the gate, and it learned that the binary
  has no CUDA only from the gate's output afterwards. On a HIP-only or
  SYCL-only build directory it therefore waited for a device it cannot use and
  hit its 120 s timeout whenever the lock was busy. It now reads Meson's
  option record of the build directory first and skips at once when
  `enable_cuda` is off.
