---
paths:
  - core/src/feature/hip/integer_psnr_hip.c
  - core/src/feature/hip/integer_psnr_hip.h
invariant: integer_psnr_hip mirrors CUDA call-graph and splits uint64 warp reduction into uint32 shuffles under HAVE_HIPCC.
---
# Integer PSNR Feature Extractor and Split-Shuffle

- **`integer_psnr_hip.c` mirrors `integer_psnr_cuda.c`
  call-graph-for-call-graph** (fork-local, ADR-0241). Same
  `PsnrStateHip`/`PsnrStateCuda` fields in same order, same
  template-helper invocations in same init/submit/collect/close
  sequence, same `provided_features` contract (`psnr_y` luma-only in
  v1). Runtime PR (T7-10b) flips `kernel_template.c` bodies;
  consumer's call sites stay verbatim. **On rebase**: keep call
  graph aligned with CUDA twin. If future PR drifts CUDA twin's
  lifecycle (e.g. adds third event), update HIP twin in same PR.

## Rebase-sensitive invariants (batch-1 real kernels — ADR-0372)

Following invariants apply to `integer_psnr_hip.c`, which was
promoted from `-ENOSYS` scaffold to real HIP Module API consumer
in ADR-0372. (`float_ansnr_hip.c` was also promoted in ADR-0372 but
was removed in commit 70ed8b3ce3 / PR #38.) These add to — and
do not replace — scaffold invariants already documented above.

- **`HAVE_HIPCC` dual-path**: all `hipModule_t` / `hipFunction_t`
  state and `psnr_hip_module_load` helpers live under
  `#ifdef HAVE_HIPCC`. Without this flag, host TU compiles without
  ROCm SDK headers and `init()` returns `-ENOSYS` (scaffold posture
  preserved). Never move device-state fields outside guard — breaks
  CPU-only CI lane.
- **`integer_psnr_hip` uint64 split-shuffle**: PSNR device kernel
  splits each uint64 warp-reduction into two uint32 `__shfl_down`
  calls (GCN/RDNA warp size = 64; HIP exposes no native uint64
  shuffle). If future ROCm release adds native uint64 shuffle
  primitives, kernel can be simplified, but cross-backend numeric
  gate must pass before landing any change:

  ```bash
  python3 "$(git rev-parse --show-toplevel)/scripts/ci/run_meson_test.py" -- \
    -C build --suite=hip-parity
  ```
