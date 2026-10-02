- The agent pages for CUDA `vif` and `psnr_hvs`, HIP `cambi` and the HIP
  kernel template record the code shapes the lint work of 2026-10-02
  introduced (staged `filter1d.cu` kernels, `psnr_hvs_load_module()`,
  `cambi_hip_arena_at()`, `hip_handle.h`). `docs/state.md`: the shared
  `float_ssim` / `float_ms_ssim` frame-sum row moved to "Recently closed"
  (all six backend parts were fixed on 2026-10-02), and the HIP and CUDA lint
  row no longer lists the `filter1d.cu` function-size rows as open.
