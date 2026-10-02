- **`motion_sycl` emits `VMAF_integer_feature_motion_sad_score` and honours
  `motion_force_zero`, as the CPU `motion` extractor does.** The CPU writes
  the frame's SAD score on every frame (weighted by `motion_fps_weight`,
  capped at `motion_max_val`); the SYCL twin published it only as the debug
  `integer_motion` score, so the result of `--backend sycl --feature motion`
  lacked a key the CPU result has. With `motion_force_zero=true` the twin
  returned the measured `motion2` / `motion3` under the `_force_0` names
  where the CPU returns 0, so the shipped `vmaf_v0.6.1mfz` model scored
  76.668 on `--backend sycl` where the CPU scores 72.321 (Netflix 576x324
  pair). Both are fixed: on an Arc A380 every output of
  116 frames is bit-identical to the CPU under seven option sets, and the
  parity gate's `motion` and `motion_debug` cells now compare the SAD score.
  The kernels and the frame time are unchanged
  ([motion](docs/metrics/motion.md#output-features)).
