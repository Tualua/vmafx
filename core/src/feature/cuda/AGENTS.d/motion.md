---
paths:
  - core/src/feature/cuda/integer_motion_cuda.c
  - core/src/feature/cuda/integer_motion_cuda.h
  - core/src/feature/cuda/integer_motion_sad_cuda.c
invariant: Motion parity, dispatch bottleneck mitigation, SAD batch depth, and output sets.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# Parity invariant — motion3 CPU and CUDA moving-average paths

`integer_motion.c` (CPU) and `integer_motion_cuda.c` (CUDA) both implement
motion3 post-process as host-side moving average over blended motion2
scores. Both paths **must stay in numerical parity at places=4** (delta
≤ 1e-4, per ADR-0214). Gate enforced by
`core/test/test_cuda_motion3_parity.c`. Any change to blend formula
(`motion_blend()`), moving-average guard condition, or `motion_max_val`
clipping must mirror across both files. Same for SYCL / Vulkan /
HIP / Metal motion twins listed in Twin-update table below — same PR.

- **`integer_motion_cuda.c::motion3_postprocess_*` honours
  motion3 GPU contract** (ADR-0219). Applies CPU's host-side
  post-process to motion2 with no device-side state. Two
  invariants flow: (1) `motion_five_frame_window=true` returns
  `-ENOTSUP` at `init()`, and the option keeps `VMAF_OPT_FLAG_DEFAULT_ONLY`
  until the twin has the window, so model dispatch runs CPU `motion`
  (ADR-1478; `test_gpu_option_value_capability_contract.py`); (2) any change to `motion_blend()` /
  `motion_max_val` / moving-average must mirror across three
  GPU motion twins in same PR. See [../../AGENTS.md §"motion3_score
  GPU contract"](../../../AGENTS.md).
- **The motion SAD `cuMemsetD8Async` runs on `pic_stream`, NOT on
  `s->str`** (ADR-0358). Kernel atomic-adds into same single-int64 buffer on
  `pic_stream`; both streams `CU_STREAM_NON_BLOCKING`, have
  no event linking them, so co-locating memset on same
  stream as kernel = only thing that orders them. Since ADR-1372 both
  motion twins do this in one place: `motion_sad_launch()` in
  `integer_motion_sad_cuda.c`. Any rebase or follow-up reverting memset onto
  separate stream silently re-introduces cross-stream race.
- **`integer_motion_cuda.c::collect_fex_cuda` and `flush_fex_cuda`
  emit `motion2_score = MIN(score * motion_fps_weight, motion_max_val)`,
  NOT raw `min(prev, cur)` SAD score** (ADR-0358). Mirrors
  CPU reference at `integer_motion.c:563`.
  `motion3_postprocess_cuda` moving-average guard reads
  `frame_index > 2` (NOT `> 1`) because `frame_index`
  pre-incremented in `collect()` before helper runs. Tripped
  by non-default `motion_fps_weight ≠ 1.0` /
  `motion_moving_average = true`.
- **`motion_fps_weight` = cross-backend parity parameter** — all
  motion-family GPU twins must expose `motion_fps_weight` in their
  `VmafOption options[]` table, apply it identically: for
  `integer_motion_v2_*` (flush-based motion2), weight scales both
  `score_cur` and `score_next` before min in `flush()`; for
  `float_motion_*` (collect-based motion2), weight scales both
  `prev_motion_score` and `motion_score` before min in `collect()`
  (for index >= 2), scales `prev_motion_score` alone in `flush()`.
  When `motion_fps_weight = 1.0` (default), arithmetic =
  no-op; `places=4` cross-backend gate must continue to pass.
  If application math ever changes in CPU reference
  (`integer_motion_v2.c` / `float_motion.c`), all GPU twins must
  update in same PR. Twins in scope: `integer_motion_v2_cuda.c`,
  `integer_motion_v2_sycl.cpp`, `motion_v2_vulkan.c`,
  `integer_motion_v2_hip.c`, `integer_motion_v2_metal.mm`,
  `float_motion_cuda.c`, `float_motion_sycl.cpp`,
  `float_motion_vulkan.c`, `float_motion_hip.c`,
  `float_motion_metal.mm`. PR #863 initially wired this option.
- **`motion_fps_weight` applied EXACTLY ONCE on v1
  `integer_motion_*` twins** (ADR-1216) — CPU reference
  (`integer_motion.c`) scales SAD-derived score by weight in
  `extract()`, stores *weighted* value as `motion_sad_score`.
  Then blends already-weighted value into `motion2` / `motion3`
  in `flush()` without touching weight again. GPU twins mirror
  this: every caller of `motion3_postprocess_{cuda,sycl,hip}()` hands
  it value already fps-weighted and `motion_max_val`-clipped,
  so **`motion3_postprocess_*` must not multiply by
  `motion_fps_weight`**. It did until ADR-1216, squaring weight in
  `motion3_score`. Because default = `1.0` and `1.0² = 1.0`, no
  default-options parity test can see this. Guard =
  `test_{cuda,sycl,hip}_motion3_parity`
  `test_motion3_fps_weight_applied_once` variant, which pins
  `motion_fps_weight = 0.6`, reads derived
  `integer_motion3_mfw_0.6` key. Keep that variant when touching these
  twins; deleting it re-opens blind spot.

## RC3 CPU parity: motion order, option tables, tiny frames (ADR-1372, ADR-1373, ADR-1374)

- **One motion SAD kernel, both motion twins** (ADR-1372). `motion_cuda` +
  `motion_v2_cuda` call `vmaf_cuda_motion_sad_submit()`
  (`integer_motion_sad_cuda.c`) = only host code loading / launching
  `integer_motion_v2/motion_v2_score.cu`. Kernel stages `prev - cur`
  (operand order load-bearing: arithmetic shift rounds negative sums down),
  then CPU `>> bpc` vertical + `>> 16` horizontal rounding. Never restore
  per-frame blur or second motion kernel; `test_cuda_motion_tiny_frames`
  compares `==` vs scalar CPU. Module owner in
  `test_cuda_module_lifecycle_contract.py` = `integer_motion_sad_cuda.c`.
- **Ping-pong ordered on device.** Submit waits on previous frame's event
  (`motion_cuda`: `event`; `motion_v2_cuda`: `lc.submit`) before
  overwriting one raw slot + reading other. Keep `prev_done` even though
  engine per-frame `cuCtxSynchronize` (ADR-1199) covers it today. No
  `cuStreamSynchronize` / `cuCtxSynchronize` / collect wait in any submit
  path.
- **Motion debug score = CPU SAD score:** `MIN(sad * motion_fps_weight,
  motion_max_val)`, per `integer_motion.c::extract`.
- **`motion_cuda` output set = CPU `motion` output set.**
  `VMAF_integer_feature_motion_sad_score` appended EVERY frame (0 at frame
  0, 0 under `motion_force_zero`, else `MIN(sad * motion_fps_weight,
  motion_max_val)`), in `extract_force_zero()`,
  `motion_collect_first_frame()`, `emit_batch_scores()`; debug
  `motion_score` = same value. Upstream adding / renaming `motion` output
  -> same change here, same PR. Guard: `test_cuda_motion_sad_score` (11
  frames = batch boundary + flush tail, `==`, 4 option sets).

## Motion kernel dispatch bottleneck (Research-0760)

- **Motion kernel (then `calculate_motion_score_kernel_8bpc`; since
  ADR-1372 shared `motion_v2_kernel_8bpc`) dispatch-bottlenecked at all
  resolutions below 4K.** ncu profile (2026-05-29, RTX 4090) shows GPU busy fraction <1% of
  wall time at 576p (kernel 7 µs, dispatch ~12.7 ms/frame). CUDA/CPU ratio = 0.22×
  at 576p, 5.8× at 4K — crossover entirely explained by dispatch overhead.
  Any optimization not addressing per-frame dispatch will not improve
  sub-4K throughput regardless of kernel-level changes. Primary fix =
  multi-frame SAD batching (accumulate N frames before readback synchronization).
  See [Research-0760](../../../../../docs/research/0760-cuda-motion-ncu-multi-resolution-20260529.md).

## Motion SAD batch fencing — MOTION_BATCH_DEPTH invariants (ADR-0845)

- **`integer_motion_cuda.c` uses MOTION_BATCH_DEPTH=8 per-slot SAD buffers to
  reduce cuStreamSynchronize from once-per-frame to once-per-8-frames (ADR-0845).**
  Key invariants must be preserved on rebase:

  1. `sad[MOTION_BATCH_DEPTH]` = ring of independent device buffers (not single
     shared accumulator). Each submit() zeroes `sad[index % MOTION_BATCH_DEPTH]`
     on pic_stream BEFORE kernel launch, so memset and atomicAdd on
     same stream (per ADR-0358 / AGENTS.md "integer_motion_cuda.c::submit_fex_cuda
     runs the SAD cuMemsetD8Async on pic_stream" invariant).
  2. `s->str` = readback drain stream. Every submit() chains its kernel-complete
     event from pic_stream to s->str via `cuStreamWaitEvent`. DtoH copies
     NOT queued in submit(); queued in batch-boundary collect() calls.
  3. Non-boundary collect() calls (where `index % MOTION_BATCH_DEPTH != MOTION_BATCH_DEPTH-1`)
     increment frame_index, return 0 without emitting scores or touching s->str.
     Emitting from non-boundary collects would break batch fence.
  4. `emit_batch_scores()` temporarily overrides `s->frame_index` to `i + 1` for
     each frame `i` in batch before calling `motion3_postprocess_cuda()`. This
     preserves moving-average guard semantics (ADR-0219). Removing or bypassing
     this frame_index override produces incorrect motion3 scores when
     `motion_moving_average=true`.
  5. drain_batch engine-scope optimization (ADR-0242) NOT used by
     integer_motion_cuda after ADR-0845. Do not re-add `vmaf_cuda_drain_batch_register_event`
     calls to submit() — would conflict with batch fence logic.
  6. flush() handles final partial batch for frame counts not
     multiple of MOTION_BATCH_DEPTH. `flush_start` clamp to 1 skips frame 0
     (no SAD: frame 0 only stages luma; kernel skipped).
  7. Both readback paths go through `motion_readback_slots()`: copies
     queue on `s->str` behind every chained frame event, then ONE
     `cuStreamSynchronize` (ADR-1372). Never re-add wait before copies;
     `test_cuda_kernel_source_contract.py` counts it.
