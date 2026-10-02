---
paths:
  - core/src/feature/cuda/integer_motion_v2_cuda.c
  - core/src/feature/cuda/integer_motion_v2_cuda.h
invariant: Motion v2 CPU mirror contract and score emission parity.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# Motion v2 mirror contract and score emission

- **`integer_motion_v2_*` mirror contract** (ADR-0662) — CPU
  `integer_motion_v2.c::mirror` maps `idx >= size` to
  `2 * size - idx - 2`. CUDA, SYCL, and Vulkan `motion_v2`
  kernels must keep that same high-edge literal. Old `-1`
  formula = stale prose from ADR-0193 bring-up, creates
  measurable CPU/GPU drift.
- **`integer_motion_v2_cuda.c::flush_fex_cuda` emits `motion3_v2_score`,
  mirrors CPU `integer_motion_v2.c::flush()` formula
  byte-for-byte** (ADR-1108). CUDA twin computes
  `motion3_v2_score` host-side over kernel's SAD scores, using
  same per-frame `motion_blend(motion2, motion_blend_factor,
  motion_blend_offset)` + `MIN(…, motion_max_val)` clip + `stamp_value`
  seeding for `i < min_idx` (`min_idx = 1`) + optional 2-tap
  `motion_moving_average`. Four options
  (`motion_blend_factor`/`motion_blend_offset`/`motion_max_val`/
  `motion_moving_average`) mirror CPU `VmafOption[]` table exactly.
  Any change to CPU `motion_v2` flush blend/clip/seed/average logic
  must mirror here in same PR to keep `places=4` parity
  gate (`test_cuda_motion_v2_parity`) green. `motion2_v2_score`
  emitted via `append_with_dict` (not bare `append`), so sfr/hfr
  co-schedule names match CPU path. SYCL/HIP/Metal twins still emit
  only `sad` + `motion2_v2`; closing that gap = follow-up that must
  reuse this same host-side formula. Unlike v1 `motion_cuda` flush,
  this = batch loop over collected SAD scores (no per-frame streaming
  post-process / `frame_index` override).

- **`motion_v2_cuda` publishes CPU SAD score** `MIN(sad * mfw, mmxv)` in
  collect; flush derives `motion2_v2` / `motion3_v2` from stored values,
  NO re-weighting, one-frame input -> 0 / 0 (`n_frames == 0` early out
  only). Mirrors `integer_motion_v2.c::extract` / `flush`.
