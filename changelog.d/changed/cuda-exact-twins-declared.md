- **Six more CUDA twins are held to the CPU's bits by the parity gate.**
  `motion_cuda` (also with `debug=true`), `motion_v2_cuda`, `psnr_cuda`,
  `float_ssim_cuda` and `float_ms_ssim_cuda` (with and without `enable_lcs`)
  and `cambi_cuda` return the CPU extractor's scores bit for bit: measured on
  an RTX 4090 at `--precision max` on 196 frames from 40x40 to 3840x2160 at 8
  to 16 bits, full-range noise included, on 200 frames of BBB 3840x2160 and
  under 18 option sets. They are now listed as exact twins, so the gate
  compares them with tolerance 0 where it allowed 5e-5, and
  `test_cuda_exact_twins` asserts equality on a device. The sweep behind it
  covered all 21 gate features and found three defects, fixed separately
  (`float_moment_cuda` and `float_psnr_cuda` on high-bit-depth content, a
  missing `motion` output). With the twins made exact by their own changes,
  every CUDA gate feature is exact except `ciede` (within 1.4e-11) and
  `speed_chroma` (within 1.4e-6), which differ by the math library only
  ([ADR-1457](docs/adr/1457-cuda-exact-twins-declared.md),
  [CUDA backend](docs/backends/cuda/overview.md#exact-twins-declared-as-a-group-2026-10-02)).
