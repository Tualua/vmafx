- **Python harness: feature discovery, options and filters as Netflix has them
  (ports of Netflix/vmaf `d327ed67b`, `3dee96664`, `560c4e491`, `5c7770080`).**
  A feature reported under several option suffixes now gives one result key
  per suffix instead of only the shortest one; the `Cambi_FR_feature` key of
  the distorted CAMBI score is `..._cambi_*` again instead of
  `..._cambi_encbd*`. `VmafFeatureExtractor` passes `vif_prescale`,
  `vif_prescale_method`, `adm_bypass_cm`, `adm_adm3_apply_hm`, `adm_p_norm`,
  `adm_skip_aim_scale`, `motion_add_scale1` and `motion_add_uv` to the
  executable, and `VmafIntegerFeatureExtractor` passes `adm_skip_aim`; before,
  they were dropped without a word. `Asset` accepts `select_cmd` and runs the
  FFmpeg filters in Netflix's order (`format_cmd` and `fps_cmd` after
  `gblur_cmd` ... `yadif_cmd`). `TrainTestModel` takes a
  `chroma_correction_parameter`. Documented in `docs/usage/python.md`.
