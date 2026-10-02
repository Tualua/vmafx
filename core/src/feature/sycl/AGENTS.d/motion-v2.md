---
paths:
  - core/src/feature/sycl/integer_motion_v2_sycl.cpp
  - core/test/test_sycl_motion_v2_parity.c
invariant: integer_motion_v2_sycl.cpp reads the shared frame and emits motion3_v2_score host-side in collect and flush.
---
<!-- markdownlint-disable MD013 MD060 -->
# motion3_v2 cross-twin invariant (ADR-1108)

- `integer_motion_v2_sycl` emits `motion3_v2_score` host-side in
  flush, mirroring CPU `integer_motion_v2.c::flush` and CUDA twin
  byte-for-byte: per-frame `motion_blend(motion2, blend_factor,
  blend_offset)` then `MIN(_, motion_max_val)` clip, a `stamp_value` seed
  for `i < min_idx (= 1)`, and optional 2-tap `motion_moving_average`,
  via shared `motion_blend_tools.h` helper. Any change to CPU
  flush blend/clip/seed/average logic must mirror into all four GPU
  twins (cuda/sycl/hip/metal) in same PR to keep `places=4`
  `test_sycl_motion_v2_parity` gate green.

- **`integer_motion_v2_sycl.cpp` reads the shared frame** (ADR-1369). `cur`
  = `vmaf_sycl_get_shared_plane(state, 1, 0)` behind
  `vmaf_sycl_queue_after_upload()`; the ADR-1371 pipeline's `cur_copy` keeps it
  in `d_pix[index % 2]` as the next frame's `prev` (`enqueue_copy` on frame 0).
  No host copy, no private upload; the kernel stays in
  `integer_motion_pipeline_sycl.cpp`.

| SYCL TU | CPU TU | Parity test | ADR |
|---|---|---|---|
| `integer_motion_v2_sycl.cpp` | `integer_motion_v2.c` | `test_sycl_motion_v2_parity.c` | ADR-0884 (round 2) |

| Kernel TU | Parity test | ADR |
|---|---|---|
| `integer_motion_v2_sycl.cpp` | `test_sycl_motion_v2_parity.c` | ADR-0884 |
