- **`float_adm_sycl` returns the CPU's scores bit for bit.** The SYCL float
  ADM twin was up to 1.7e-5 from the CPU extractor: it associated the angle
  test's threshold differently, used `float` where the CPU uses `double` for the
  enhancement gain and two constants, added the masking threshold and the
  frame sums in another order, and floored the frame sums at `1e-2` where the
  CPU uses `1e-10`. The kernels now run the CPU's arithmetic operation for
  operation; a SYCL kernel has no `double`, so the three `double` expressions
  are evaluated as exact pairs of `float` values, with the CPU's operations
  replayed in 64-bit integers next to a rounding boundary
  ([ADR-1434](docs/adr/1434-sycl-float-adm-cpu-arithmetic.md)). Measured on
  an Arc A380 at `--precision max`, every output of every frame equals the
  CPU extractor of the same build on the Netflix 576x324 pair at 8, 10, 12
  and 16 bits, both 1080p checkerboard pairs and 200 frames of BBB
  3840x2160, with `debug=true`. A 3840x2160 frame takes 12.3 ms instead of
  15.1 ms; the twin uses 48 MB more device memory there. On near-flat
  content scored with `adm_noise_weight=0` the twin reported `adm2 = 1`
  where the CPU reports 0; it now reports the CPU's value. The twin also
  takes the CPU options it rejected: `adm_skip_scale0`, `adm_skip_aim_scale`,
  `adm_f1s0..3` and `adm_f2s0..3`. `adm_p_norm` other than 1 or 3 stays
  within 1.8e-7 of the CPU. Stored `float_adm_sycl` scores change by up to
  1.7e-5.
