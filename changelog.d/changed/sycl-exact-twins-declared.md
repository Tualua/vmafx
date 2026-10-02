- **Six more SYCL twins are held to the CPU's bits by the parity gate.**
  `adm_sycl`, `motion_sycl` (also with `debug=true`), `motion_v2_sycl`,
  `psnr_sycl`, `float_ssim_sycl` (with and without `enable_lcs`) and
  `cambi_sycl` return the CPU extractor's scores bit for bit: measured on an
  Arc A380 at `--precision max` on 333 frames from 576x324 to 3840x2160 at 8
  to 16 bits, full-range noise included. They are now listed as exact twins,
  so the gate compares them with tolerance 0 where it allowed 5e-5, and
  `test_sycl_exact_twins` asserts equality on a device. With the twins made
  exact earlier, 19 of the 21 gate features are exact on SYCL; `ciede` is
  within its 1.4e-11 bound and `speed_chroma` depends on the build's math
  library. With the default model the VMAF score of every measured frame
  equals `--backend cpu`
  ([ADR-1451](docs/adr/1451-sycl-exact-twins-declared.md),
  [SYCL backend](docs/backends/sycl/overview.md#exact-twins-declared-as-a-group-2026-10-02)).
