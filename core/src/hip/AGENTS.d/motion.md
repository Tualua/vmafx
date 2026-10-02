---
paths:
  - core/src/feature/hip/integer_motion_v2_hip.c
  - core/src/feature/hip/float_motion_hip.c
invariant: Motion HIP extractors mirror CUDA twins with ping-pong buffering and cross-backend motion_fps_weight parity.
---
# Motion Feature Extractors and Parity Invariants

## Rebase-sensitive invariants (fifth + sixth consumers)

- **`float_ansnr_hip.c` (removed)**: fifth consumer per ADR-0266 was
  `float_ansnr_hip.c`, which mirrored `float_ansnr_cuda.c`. Both
  removed in commit 70ed8b3ce3 (PR #38). No-memset bypass invariant
  (`submit_pre_launch` not called; per-block `(sig, noise)` float
  partials) applied to that TU — see
  [ADR-0266](../../../../docs/adr/0266-hip-fifth-consumer-float-ansnr.md)
  for historical rationale. On rebase, ignore `float_ansnr`-related
  hunks.
- **`integer_motion_v2_hip.c` mirrors `integer_motion_v2_cuda.c`
  call-graph-for-call-graph** (fork-local, ADR-0267). Carries
  `VMAF_FEATURE_EXTRACTOR_TEMPORAL` flag and `flush()` callback.
  `uintptr_t pix[2]` ping-pong slots are fork-local scaffold-shape —
  runtime PR (T7-10b) will land HIP device-buffer allocator and
  replace these with real handles matching CUDA twin's
  `VmafCudaBuffer *pix[2]` field shape. **On rebase**: keep field
  count and slot type aligned with CUDA twin; ping-pong contract
  (cur = `index % 2`, prev = `(index + 1) % 2`) is load-bearing for
  eventual cross-backend numeric gate.

## Rebase-sensitive invariants (seventh + eighth consumers)

- **`float_motion_hip.c` mirrors `float_motion_cuda.c`
  call-graph-for-call-graph** (fork-local, ADR-0273). State struct
  carries three `uintptr_t` buffer slots (`ref_in`, `blur[2]`)
  tracked outside kernel-template's readback bundle; runtime PR
  (T7-10b) will swap them for real device-buffer handles matching
  CUDA twin's `VmafCudaBuffer *ref_in` + `VmafCudaBuffer *blur[2]`
  field shape. Submit path **intentionally does not call
  `vmaf_hip_kernel_submit_pre_launch`** (kernel writes per-WG SAD
  float partials directly, no atomic, no memset) — same bypass as
  `ciede_hip` and `float_ansnr_hip`. `motion_force_zero`
  short-circuit (`fex->extract` swap with
  `submit / collect / flush / close` nulled) is load-bearing and
  must stay aligned with CUDA twin. **On rebase**: any drift in
  CUDA twin's buffer-slot count or
  `motion_force_zero` posture requires paired update here.
- **`motion_fps_weight` cross-backend parity** — see canonical
  invariant note in
  [`../feature/cuda/AGENTS.md`](../../feature/cuda/AGENTS.md).
  `integer_motion_v2_hip.c` and `float_motion_hip.c` both carry
  `motion_fps_weight` option and apply it identically to CUDA /
  SYCL / Metal twins. Any future change to weight
  application math must span all current motion-family GPU twins in same
  PR.
