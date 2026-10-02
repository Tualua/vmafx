- **`float_ssim_sycl` returns the CPU's `float_ssim` on frames whose
  per-window terms cancel.** The twin was declared exact and matched the CPU
  on every frame of real content measured, but on a constructed 64x64 pair it
  scored one `float` step away (-4.222829659283889e-07 where the CPU scores
  -4.222829943500983e-07): the CPU adds one `double` term per window in
  raster order, and the twin added the terms per work-group, which rounds
  elsewhere. The twin now forms each window's terms as the CPU's doubles and
  the host adds them in the CPU's order, for `float_ssim` and for
  `float_ssim_l`, `_c` and `_s` under `enable_lcs`, at every `scale`. On an
  Arc A380 the constructed pair and 2070 of 2070 values on 138 frames under
  six option sets are bit-identical to the CPU. With the automatic scale a
  1080p frame takes 1.51 ms instead of 1.32 and a 4K frame 4.11 ms instead of
  3.93; an explicit `scale=1` on a 4K frame takes 39.1 ms instead of 23.3 ms
  (48.6 instead of 25.5 ms with `enable_lcs`)
  ([SYCL backend](docs/backends/sycl/overview.md#float_ssim_sycl-adds-its-frame-sums-in-the-cpus-order-2026-10-02),
  [ADR-1463](docs/adr/1463-sycl-float-ssim-raster-sum.md)).
