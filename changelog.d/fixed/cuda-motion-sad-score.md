- **`motion_cuda` emits `VMAF_integer_feature_motion_sad_score`, as the CPU
  `motion` extractor does.** The CPU writes the frame's SAD score on every
  frame (weighted by `motion_fps_weight`, capped at `motion_max_val`); the
  CUDA twin computed it and published it only as the debug `integer_motion`
  score, so the result of `--backend cuda --feature motion` lacked a key the
  CPU result has. It now writes it on every frame, bit-identical to the CPU
  on 348 of 348 measured frames on an RTX 4090, also with `debug`,
  `motion_force_zero`, `motion_moving_average` and weight, blend and cap
  options. `motion2` / `motion3` and the frame time are unchanged
  ([motion](docs/metrics/motion.md#output-features)).
