- **`float_vif_sycl` returns the CPU's scores bit for bit.** The SYCL twin
  filtered with a table of Gaussian taps the CPU extractor stopped using
  (it derives them at start-up with `vif_get_filter()`), called the device
  `log2` where the CPU evaluates a polynomial, took `vif_sigma_nsq` as a
  `float` where the CPU keeps it in `double`, and reduced per sub-group and
  per block where the CPU keeps one `float` running sum per row and one over
  the rows. Measured on an Arc A380 that left no frame of the Netflix
  576x324 pair identical, up to 3.8e-5 away, and 7.0e-6 at 3840x2160. The
  twin now takes the CPU's taps, evaluates the CPU's per-pixel statistic
  without a 64-bit floating-point type (pairs of floats, and the CPU's
  `double` operations replayed in integers next to a rounding boundary) and
  adds in the CPU's order
  ([ADR-1422](docs/adr/1422-sycl-float-vif-cpu-arithmetic.md), after
  ADR-1412 for CUDA). At `--precision max` every output of every frame is
  identical on the Netflix pair at 8, 10, 12 and 16 bits, both 1080p
  checkerboard pairs and 200 frames of BBB 3840x2160, with `debug=true` and
  with non-default options. `float_vif_sycl` also accepts the CPU's
  `vif_scale1_min_val`, `vif_scale2_min_val` and `vif_scale3_min_val` now. A
  3840x2160 frame takes 23.95 ms on the A380, 20.54 ms before, and 100 MB
  more device memory. The parity gate compares this twin with tolerance 0.
  Stored `float_vif_sycl` outputs change by up to 3.8e-5. The HIP and Metal
  twins still agree with the CPU to four decimal places.
