- **`--backend hip` runs the default model's ADM on the GPU, with the CPU's
  values.** `adm_hip` had no AIM contrast-measure pass, so it could not emit
  `aim` and `adm3`, the ADM features the default model `vmaf_v1.0.16_3d0h`
  reads, and it carried no HIP flag, so `--backend hip` never selected it and
  the model's ADM ran on the CPU. The twin now computes AIM on the device
  with the CUDA twin's kernels, takes every rounding shift and its per-scale
  conclusion from the CPU extractor's own routines, accepts `adm_skip_aim`,
  and is selected by `--backend hip` and by `--feature adm`. Every output,
  `aim` and `adm3` included, is bit-identical to `--backend cpu` (4141 of
  4141 values on a gfx1036), and the default model's VMAF under
  `--backend hip` equals the CPU's on every frame measured. On the
  integrated gfx1036 the twin is slower than the 16-thread CPU extractor
  (218 against 14.5 ms per 3840x2160 frame); tuning is tracked for the
  benchmark candidate
  ([ADR-1525](docs/adr/1525-adm-hip-aim-device-pass.md)).
