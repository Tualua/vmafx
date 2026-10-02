---
paths:
  - core/test/test_cuda_buffer_alloc_oom.c
  - core/test/test_cuda_pic_preallocation.c
  - core/test/test_cuda_speed_chroma_smoke.c
  - core/test/test_cuda_speed_temporal_smoke.c
  - core/test/test_gpu_dispatch_runtime.c
invariant: GPU tests must skip gracefully when no device present; GPU-only extractors get smoke gate, not parity gate.
---
<!-- markdownlint-disable MD013 -->
# GPU test skip guards, smoke gates, and dispatch runtime

- **GPU tests must skip gracefully when no device present.** Any
  test calling `vmaf_cuda_state_init`, `vmaf_hip_state_init`, or
  equivalent GPU-init helpers must check return value before
  proceeding. On failure (`err != 0` or returned pointer is NULL),
  emit `[skip: no CUDA/HIP/Vulkan device]` to stderr and
  `return NULL` — do not hard-fail via `mu_assert`. Replacing
  hard-fail `mu_assert` with skip guard is one-line pattern; see
  `test_cuda_buffer_alloc_oom.c` and `test_cuda_pic_preallocation.c`
  for reference. **Rebase-sensitive**: any new GPU test lacking this
  guard will SIGSEGV on CPU-only CI runners.
- **GPU-only extractors get smoke gate, not parity gate.** When CUDA
  / HIP / SYCL feature extractor has no CPU twin emitting same
  feature name (e.g. `speed_chroma_cuda`, `speed_temporal_cuda` —
  emit `Speed_*_feature_*_score`, no CPU producer), CPU-vs-GPU parity
  assertion is wrong tool. Gate is smoke test: register extractor,
  run multi-frame fixture, assert finite scores at frame index 1.
  Catches high-impact failure modes (NaN/Inf drift from kernel grid
  changes or covariance-matrix degenerate cases) without inventing
  redundant CPU reference. See `test_cuda_speed_chroma_smoke.c` /
  `test_cuda_speed_temporal_smoke.c` (ADR-0956). Fixture sizing
  matters here: speed kernels need 640x360+ to admit non-singular
  covariance matrix in ADR-0567 host-side eigendecomp path.
  **Rebase-sensitive**: do not "fix" smoke test by adding fake CPU
  twin — ADR-0956 alternatives table documents why.
  calls `vmaf_cuda_state_init`, `vmaf_hip_state_init`,
  `vmaf_metal_state_init`, or equivalent GPU-init helpers must check
  return value before proceeding. On failure (`err != 0` or returned
  pointer is NULL), emit `[skip: no CUDA/HIP/Metal/Vulkan device]` to
  stderr and `return NULL` — do not hard-fail via `mu_assert`.
  Replacing hard-fail `mu_assert` with skip guard is one-line
  pattern; see `test_cuda_buffer_alloc_oom.c`,
  `test_cuda_pic_preallocation.c`, `test_sycl_motion3_parity.c`, and
  Metal parity tests `test_metal_*_parity.c` for reference.
  **Rebase-sensitive**: any new GPU test lacking this guard will
  SIGSEGV on CPU-only CI runners (and on macOS Intel runners, where
  Metal returns `-ENODEV`).
- **GPU dispatch-runtime test mutates process env.**
  [`test_gpu_dispatch_runtime.c`](../test_gpu_dispatch_runtime.c) calls
  `setenv()` on `VMAFX_TEST_DISPATCH_RUNTIME_*` keys + real
  `VMAF_CUDA_DISPATCH` to exercise once-snapshot semantics. Snapshot
  table is process-wide singleton (ADR-0488) so first
  `vmaf_gpu_dispatch_env_get(key)` call wins permanently — tests must
  pre-set env BEFORE first selector call. Test executable is
  fork-local (no upstream coupling); namespaced `VMAFX_TEST_*` keys
  prevent collisions with production `VMAF_*_DISPATCH` variables. See
  [ADR-0954](../../../docs/adr/0954-gpu-runtime-coverage-test.md).
