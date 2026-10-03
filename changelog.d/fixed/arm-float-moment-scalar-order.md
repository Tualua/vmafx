- **The aarch64 CPU `float_moment` returns the scalar function's bits on
  every input and SVE vector length.** The NEON kernel added into lane
  accumulators and the SVE2 kernel added per-row vector sums, grouped by the
  vector length. On a 16-bit frame whose sum of squares passes 2^53 units
  (more than 2 097 152 pixels) the scalar `double` rounds on every add, so
  their second moments (`float_moment_ref2nd`, `float_moment_dis2nd`) could
  differ from the x86 and scalar results in the last digits (by 2.1e-6 on a
  second moment of 37918 for a 16-bit 3841x2160 test frame, under
  `qemu-aarch64`). Both kernels now add
  each value into one `double` in raster order, as the x86 kernels do.
  `test_moment_simd` asserts `==` for AVX2, AVX-512, NEON and SVE2 at 128,
  256, 512 and 2048 bits; its SVE2 case, and the NEON cases of
  `test_iqa_convolve`, had been skipped on every processor because they read
  the CPU flags before `vmaf_init_cpu()`. 8-, 10- and 12-bit scores and x86
  builds do not change. The kernels now run at about the scalar loop's speed
  ([ADR-1500](docs/adr/1500-arm-float-moment-scalar-order.md),
  [Arm backend](docs/backends/arm/overview.md#bit-exactness)).
