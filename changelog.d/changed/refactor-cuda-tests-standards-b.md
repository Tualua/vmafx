- **Refactor CUDA test files part 2 for clang-tidy and HISS standard compliance (ADR-1142).**
  Brings 10 CUDA test files (`test_cuda_ssim_parity.c`, `test_cuda_speed_chroma_smoke.c`,
  `test_cuda_speed_singular_parity.c`, `test_cuda_speed_temporal_parity.c`,
  `test_cuda_speed_temporal_smoke.c`, `test_cuda_drain_batch.c`,
  `test_cuda_picture_pinned_overflow.c`, `test_cuda_buffer_alloc_oom.c`,
  `test_cuda_single_frame_flush.c`, `test_cuda_arch_floor.c`) to 0 warnings in the
  `cuda` lane, tightening the baseline by 104 warnings (from 1110 to 1006).
  Applies file-level ADR-1138 `modernize-use-nullptr` brackets, isolates variable declarations,
  and extracts helpers to satisfy function size and branch complexity constraints.
