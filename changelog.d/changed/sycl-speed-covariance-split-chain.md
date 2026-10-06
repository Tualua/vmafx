- **SYCL: the exact SpEED covariance costs 58.7 % less time.** The covariance
  is still the CPU's sequential fp64 sum, bit for bit. Its differences and
  products now run in parallel, and only the add chain stays sequential
  (ADR-1931). On an
  Arc A380, QSV zero-copy, 3840x1600 10-bit with `vmaf_v1.0.16_3d0h`, the
  filter's GPU time per frame is 22.6 ms, down from 28.6 ms with the exact fix
  alone and 18.5 ms before that fix
  (`T-SYCL-SPEED-COV-EXACT-SEQUENTIAL-COST-2026-10-06`).
