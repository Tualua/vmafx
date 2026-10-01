- **Five more HIP twins are held to the CPU's bits by the parity gate.**
  `motion_hip` (also with `debug=true`), `motion_v2_hip`, `psnr_hip`,
  `integer_ms_ssim_hip` (the HIP twin of `float_ms_ssim`, with and without
  `enable_lcs`) and `cambi_hip` return the CPU extractor's scores bit for bit:
  measured on a gfx1036 at `--precision max` on 178 frames from 480x270 to
  3840x2160 at 8 to 16 bits, and with their options. They are now listed as
  exact twins, so the gate compares them with tolerance 0 where it allowed
  5e-5, and `test_hip_exact_twins` asserts equality on a device. The same
  sweep found `float_psnr_hip` and `float_moment_hip` identical on real clips
  but not on all input (up to 7.6e-8 dB at 10 to 16 bits with large
  differences; second moments up to 1.0e-4 at 16 bits); they stay under their
  tolerance. The table of every HIP twin is in
  [the HIP backend page](docs/backends/hip/overview.md#which-hip-twins-return-the-cpus-bits-2026-10-01)
  ([ADR-1437](docs/adr/1437-hip-exact-twins-declared.md),
  [Research-1437](docs/research/1437-hip-twin-exactness-sweep.md)).
