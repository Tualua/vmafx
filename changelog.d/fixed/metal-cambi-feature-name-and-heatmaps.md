- **`--backend metal` reports CAMBI under the CPU's feature name, and the Metal
  twin writes heatmaps.** `integer_cambi_metal` built its feature names after it
  had written the resolved encode and source sizes into the `enc_width`,
  `enc_height`, `enc_bitdepth`, `src_width` and `src_height` option slots, so
  every name carried them: a 576x324 8-bit run reported
  `cambi_encbd_8_ench_324_encw_576_srch_324_srcw_576` instead of `cambi`, and a
  run of the default model `vmaf_v1.0.16_3d0h` on Metal stopped with "problem
  generating pooled VMAF score" because the model found no CAMBI score. The
  names are now built first, as the CPU extractor builds them. The twin also
  accepts `heatmaps_path`: it writes the per-scale `.gray` heatmaps with the CPU
  extractor's own writer, so the files equal those of `--backend cpu`. The CPU
  extractor's scores and heatmap files are unchanged; its close now reports
  `-EIO` when a heatmap file fails to close. Found by the Apple M4 Pro tester
  report (#2118); the Metal change is source only and waits for a device re-run
  (`T-METAL-CAMBI-SCORE-NAME-SUFFIXED-2026-10-05`,
  `T-BUG048-GPU-OPTION-PARITY-REMAINDER-2026-09-26`).
