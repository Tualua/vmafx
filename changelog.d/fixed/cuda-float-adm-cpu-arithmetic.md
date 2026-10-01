- **`float_adm_cuda` returns the CPU's scores bit for bit.** The CUDA twin
  of `float_adm` was up to 1.3e-5 from the CPU extractor (`adm_scale0` at
  3840x2160) and matched it on 144 of 791 measured scores. Nine things
  differed: the association of the angle test's threshold (the 1.3e-5), the
  order of the sums, CSF weights from a copied formula that rounded
  differently, a true division where the CPU multiplies by a refined
  reciprocal estimate, the order of the masking threshold's terms, `float`
  constants and a `float` gain limit where the CPU uses `double`, and a
  floor of the frame sums at `1e-2` where the CPU's is `1e-10`. The last one
  could report `adm2 = 1` where the CPU reports 0, for content with almost no
  reference detail scored without the noise floor. The twin now runs the
  CPU's arithmetic in the CPU's types, adds each row on the device and the
  rows on the host in the CPU's order, and takes the weights, the reduced
  region, the pooling and the floor from the CPU's own routines
  ([ADR-1420](docs/adr/1420-cuda-float-adm-cpu-arithmetic.md)). The CPU's
  division is built on the processor's `RCPSS` estimate, so the twin probes
  that estimate when the extractor starts (about 10 ms) and evaluates it on
  the device. Measured on an RTX 4090 at `--precision max`: every output of
  every frame identical on the Netflix pair at 8, 10, 12 and 16 bits, both
  1080p checkerboard pairs and BBB 3840x2160, also with `debug=true` and
  with non-default `adm_enhn_gain_limit`, `adm_bypass_cm`,
  `adm_noise_weight`, `adm_skip_aim_scale` and viewing geometry. The parity
  gate compares this twin with tolerance 0. Not identical: `adm_p_norm`
  other than 1 or 3, where the twin is within 1.1e-7 of the CPU (the two
  `powf` implementations differ). A run of the twin alone takes 1.98 ms per
  3840x2160 frame instead of 1.87 ms; its kernels take 1.11 ms instead of
  0.76 ms. Stored `float_adm_cuda` outputs change in their low digits by at
  most 1.3e-5. The SYCL, HIP and Metal twins still agree with the CPU to
  four decimal places.
