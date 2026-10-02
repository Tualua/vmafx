---
paths:
  - core/src/feature/hip/integer_motion_sad_hip.c
  - core/src/feature/hip/integer_motion_sad_hip.h
invariant: Motion SAD uses one diff-first kernel and a single launcher implementation.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# Motion SAD: one diff-first kernel, one launcher (ADR-1377)

- CPU `motion` / `motion_v2` SAD = `sum |H(V(prev - cur))|`: difference raw
  frames first, vertical 5-tap rounded `>> bpc` (int32 sum at 8 bits, int64
  above), horizontal rounded `>> 16`, arithmetic shifts.
- Only kernel: `integer_motion_v2/motion_v2_score.hip`. Only host loader and
  launcher: `integer_motion_sad_hip.c` (`vmaf_hip_motion_sad_submit()`).
  `motion_hip` and `motion_v2_hip` both call it; neither TU calls
  `hipModuleLaunchKernel`.
- Do not restore per-frame blur ping-pong (`blur[2]`, `motion_score.hip`,
  pre-ADR-1377 `motion_hip`): rounds unlike CPU, 1.26e-5 on Netflix pair on
  gfx1036. Do not swap operands to `cur - prev`: arithmetic shift rounds
  negative sums toward minus infinity.
- `motion_hip` keeps raw luma ping-pong `pix[2]` + pinned `staging`; frame 0
  uploads only. `submit()` never waits; `collect()` = one host wait.
- Staged upload bound = allocated size from owner (`.staging_bytes =
  s->plane_bytes`), not frame geometry; `submit()` never rewrites
  `frame_w` / `frame_h`. Error after a copy was enqueued -> drain stream
  before returning (`*_drain_after_error()`, only in `return` of an error
  branch; contract test checks).
- Every emitted `motion_hip` score, debug `motion` included, goes through
  `motion_clip_hip()` (fps weight, then `motion_max_val`), like CPU
  `extract()`. One-frame run: `motion3[0] = 0` from `msh_flush_tail()`.
- Guards: `test_hip_motion_tiny_frames` (`==` vs scalar CPU, 3x3 to 1283x723,
  8/10/16 bit, skip 77 without device), `test_hip_kernel_source_contract.py`
  (planted regressions), `test_hip_upload_race` (motion rows).
