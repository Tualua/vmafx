- **`float_motion_sycl` emits `motion3`, like the CPU `float_motion`.** The
  SYCL twin wrote `motion` and `motion2` only, so `--backend sycl --feature
  float_motion` lost `VMAF_feature_motion3_score` without a warning, and a
  request with `motion_blend_factor` or `motion_blend_offset` ran on the CPU.
  It now publishes the CPU's `motion3` (the fps-weighted `motion2`, blended by
  `motion_blend_factor` / `motion_blend_offset` and capped at
  `motion_max_val`; frame 0 from the first SAD, `0` for a one-frame input) and
  accepts both blend options (aliases `mbf` / `mbo`). On an Arc A380 at
  `--precision max` every output equals the CPU's on the Netflix pair, both
  1080p checkerboard pairs and 200 frames of BBB 3840x2160, with and without
  the options. The cross-backend parity gate's `float_motion` cell now
  compares `motion3` too, and `test_sycl_twin_option_parity` carries the
  `motion3` cases and the regression cases of the earlier SYCL parity fixes
  (flat identical frames, a single-pixel frame, `apsnr` with `--subsample 2`,
  `motion_v2` weight, cap and one frame). The Metal twin still writes no
  `motion3` (`T-GPU-FLOAT-MOTION3-MISSING-2026-09-30`).
