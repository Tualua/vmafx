- **`float_motion_sycl` takes `motion_add_uv`.** The option (alias `mau`) blurs
  Cb and Cr at their own size from the shared chroma planes and adds the
  per-plane SAD scores to the luma score, in the CPU's order, so the SYCL twin
  returns the CPU's `motion`, `motion2` and `motion3` bit for bit (8 and 10 bit,
  measured on an Arc A380) and `libvmaf_sycl` scores
  `feature=name=float_motion:motion_add_uv=true` on QSV zero-copy input instead
  of stopping with `cannot honour option 'motion_add_uv'`. Zero-copy input needs
  a chroma-marked import (ADR-1765). See the [motion page](docs/metrics/motion.md)
  and [ADR-1767](docs/adr/1767-sycl-float-motion-add-uv.md).
