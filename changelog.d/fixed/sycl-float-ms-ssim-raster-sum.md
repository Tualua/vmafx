- **`float_ms_ssim_sycl` returns the CPU's per-scale means on frames where a
  mean lies next to a `float` rounding boundary.** `float_ms_ssim` adds each
  scale's luminance, contrast and structure terms into a running `double`
  per scale; the SYCL twin added them as integers per work-group, an exact
  sum that is not the CPU's. On a 176x176 noise pair its
  `float_ms_ssim_l_scale0` was 0.9884905219078064 where the CPU returns
  0.9884904623031616. The twin now stores every window's terms, with the
  luminance and contrast terms as the CPU's doubles, and the host adds them
  in the CPU's order. On an Arc A380, 2208 of 2208 `enable_lcs` values on
  138 frames are bit-identical to the CPU. A 3840x2160 frame takes 75.9 ms
  instead of 44.7 ms (18.2 instead of 11.5 ms at 1920x1080) and the twin
  holds 219 MB of device and pinned host memory at 3840x2160
  ([SYCL backend](docs/backends/sycl/overview.md#float_ms_ssim_sycl-adds-its-per-scale-sums-in-the-cpus-order-2026-10-02),
  [ADR-1466](docs/adr/1466-sycl-float-ms-ssim-raster-sum.md)).
