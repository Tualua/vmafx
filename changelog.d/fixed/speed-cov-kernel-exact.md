- **`speed_chroma` and `speed_temporal` compute the same covariance bits on
  every CPU dispatch level ([ADR-1459](docs/adr/1459-speed-cov-kernel-exact.md)).**
  The AVX2 and AVX-512 covariance kernels (upstream `30f472b14`) split each
  sum over vector lanes with fused multiply-adds and differed from the scalar
  kernel in the last bits of about one sum in five (up to 6.5e-12 relative),
  under a documented 1e-9 tolerance. They are replaced by row kernels that
  keep one lane per covariance sum and multiply and add separately, so every
  sum has the scalar kernel's bits; aarch64 gets the same kernel for NEON, its
  first SIMD path for SpEED. No score changed on the measured fixtures (60 of
  60 x86 reports and 68 of 68 aarch64 reports byte-identical at
  `--precision max`). The kernels are 2.3x to 4.1x faster than scalar and 1.6x
  to 2.0x slower than the ones they replace on 1080p and 2160p planes:
  `speed_chroma` + `speed_temporal` take up to 12 % more CPU time per frame on
  x86 (3840x2160, AVX-512), which `T-SPEED-COV-KERNEL-EXACT-THROUGHPUT-2026-10-02`
  tracks.
