---
paths:
  - core/src/feature/hip/ciede_hip.c
  - core/src/feature/hip/ciede_hip.h
invariant: ciede_hip mirrors CUDA twin call-graph and intentionally bypasses vmaf_hip_kernel_submit_pre_launch.
---
# CIEDE Feature Extractor Submit Bypass

## Rebase-sensitive invariants (additional consumers)

- **`ciede_hip.c` mirrors `integer_ciede_cuda.c`
  call-graph-for-call-graph** (fork-local, ADR-0259). Submit path
  **intentionally does not call `vmaf_hip_kernel_submit_pre_launch`**
  because kernel writes one float per block (no atomic, no memset
  required) — CUDA twin makes same choice. **On rebase**: if future
  PR adds `submit_pre_launch` call to `integer_ciede_cuda.c`'s
  submit path, HIP twin must follow in same PR; bypass is
  load-bearing artefact this consumer pins.
