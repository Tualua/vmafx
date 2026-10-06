- **Metal: 18 twins are declared exact after the first Apple device report.** An
  outside tester ran the macOS tester bundle on an Apple M4 Pro (issue #2118,
  `docs/hardware-reports/2026-10-05-apple-m4-pro.json`). The parity gate, holding
  every Metal cell exact at `--precision max`, measured 0 on the four fixtures for
  `float_adm`, `float_moment`, `float_motion`, `float_ms_ssim` (with `_lcs` and
  `_chroma`), `float_psnr`, `float_ssim` (with `_lcs`), `float_vif`, `motion` (with
  `motion_debug` and the five-frame window), `motion_v2` (with the five-frame
  window), `psnr`, `ssim` and `ssimulacra2`, and their parity tests passed every
  `==` case. Each is now a `scripts/ci/exact_twins.d/<feature>.metal` fragment, so
  the gate compares those Metal cells with tolerance 0 without `--hold-exact`.
  The same report, the NVIDIA (RTX 3050) and Intel (UHD 770) reports of
  2026-10-05 are under `docs/hardware-reports/`.
