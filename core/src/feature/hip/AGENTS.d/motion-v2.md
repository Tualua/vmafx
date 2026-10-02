---
paths:
  - core/src/feature/hip/integer_motion_v2_hip.c
  - core/src/feature/hip/integer_motion_v2_hip.h
  - core/src/feature/hip/integer_motion_v2/motion_v2_score.hip
invariant: motion3_v2 cross-twin invariants and score consistency must be preserved.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# motion3_v2 cross-twin invariant (ADR-1108)

- `integer_motion_v2_hip` emits `motion3_v2_score` host-side in its
  flush, mirroring CPU `integer_motion_v2.c::flush` and CUDA twin
  byte-for-byte: per-frame `motion_blend(motion2, blend_factor,
  blend_offset)` then `MIN(_, motion_max_val)` clip, a `stamp_value`
  seed for `i < min_idx (= 1)`, and optional 2-tap
  `motion_moving_average`, via shared `motion_blend_tools.h` helper.
  Any change to CPU flush blend/clip/seed/average logic must be
  mirrored into all four GPU twins (cuda/sycl/hip/metal) in same PR
  to keep `places=4` `test_hip_motion_v2_parity` gate green.
  (`test_hip_motion_v2_parity` added in PR #913 but unregistered in
  `core/test/meson.build` until wired in
  `fix/hip-motion-v2-parity-test-wiring`.)
