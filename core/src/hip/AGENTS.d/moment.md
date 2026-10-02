---
paths:
  - core/src/feature/hip/float_moment_hip.c
  - core/src/feature/hip/float_moment_hip.h
invariant: float_moment_hip mirrors CUDA twin call-graph with four-uint64 atomic-counter readback and pre-launch memset.
---
# Float Moment Feature Extractor Counter Readback

- **`float_moment_hip.c` mirrors `integer_moment_cuda.c`
  call-graph-for-call-graph** (fork-local, ADR-0260). Four-uint64
  atomic-counter readback (`MOMENT_HIP_COUNTERS = 4u`) sized at
  `init()` time. Submit path **does** call `submit_pre_launch`
  (kernel uses atomic adds, so memset of all four counters is
  mandatory). **On rebase**: keep four-counter constant aligned with
  CUDA twin's `4u * sizeof(uint64_t)` readback size; any drift in
  CUDA twin's counter count requires paired update here.
