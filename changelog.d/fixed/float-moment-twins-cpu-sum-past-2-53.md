- **`float_moment_cuda`, `float_moment_sycl` and `float_moment_hip` are
  bit-identical to the CPU `float_moment` on every frame, 16-bit frames of
  more than 2 097 152 pixels included.** The CPU adds the float squares into
  one `double` in raster order, and once that sum passes 2^53 units of 2^-16
  it rounds as it adds; the twins rounded the exact sum once and were up to
  5.1e-7 from the CPU's second moments there (0 of 16 frames of 16-bit
  3840x2160 noise and 0 of 4 of 16-bit 7680x4320 noise identical on each
  device). On such frames four more kernels now form the CPU's rounded sum
  from rows (`core/src/feature/float_moment_sum.h`), and all four outputs are
  identical on every measured frame at `--precision max` on an RTX 4090, an
  Arc A380 and a gfx1036. Frames that cannot pass 2^53 (every 8-, 10- and
  12-bit frame and every frame up to 2 097 152 pixels) run no new work. A
  16-bit 3840x2160 frame past 2^53 costs +0.03 ms on the RTX 4090, +2.8 ms on
  the A380 and +7.2 ms on the gfx1036. Stored 16-bit second moments of large
  bright frames from these twins change by up to 5.1e-7
  ([ADR-1497](docs/adr/1497-float-moment-twins-cpu-sum-past-2-53.md),
  [CUDA backend](docs/backends/cuda/overview.md#float_moment_cuda-matches-the-cpu-float_moment-past-253-units-too-2026-10-03)).
