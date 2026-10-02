---
paths:
  - core/src/feature/motion.c
  - core/src/feature/float_motion.c
  - core/src/feature/integer_motion.c
invariant: Motion plane structures, upstream options, mirror implementations, and chroma min dims.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# Motion Plane Layouts, Mirror Bodies, and Min Dims

- **`float_motion.c` planes**: `MotionState.plane[0..2]` are Y, U, V; U and V
  exist only with `motion_add_uv`, so `motion_free_planes` (only teardown)
  must keep `motion_add_uv` guard. `motion_chroma_heights` rejects
  chroma-less formats **before** any allocation. `motion_score_pair` adds
  Y, then U, then V — `double` add order is load-bearing for
  `motion_add_uv` parity with CUDA / SYCL twins. `motion_clip` /
  `motion_blend_clip` are only places `motion_fps_weight` /
  `motion_max_val` clip is applied.

- **Upstream ports**: `feature/motion` options from `b949cebf`
  (T-NEW-1) MERGED via PR #197 (2026-04-29). `feature/speed`
  port from `d3647c73` (`speed_chroma` + `speed_temporal`) is
  PR #213 (open). 32-bit ADM/cpu fallbacks (`8a289703` +
  `1b6c3886`) are PR #212 (open).
