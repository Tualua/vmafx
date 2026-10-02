---
paths:
  - core/src/feature/cuda/integer_adm_cuda.c
  - core/src/feature/cuda/integer_ssim_cuda.c
invariant: CUDA error paths must return mapped errno instead of literal error codes.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# CUDA error-path mapped errno discipline

- **CUDA error-path labels return mapped errno, not literal (2026-06-27
  bug-hunt).** Any `fail:` / `fail_pop:` / `fail_after_pop:` label reached from
  `CHECK_CUDA_GOTO` must `return _cuda_err;` — macro already set `_cuda_err`
  from `vmaf_cuda_result_to_errno(CUresult)` (`-ENOMEM` / `-ENODEV` / `-EINVAL`
  / `-EIO`). Returning literal `-EIO` discards real failure cause,
  diverges from `CHECK_CUDA_RETURN` convention in `cuda_helper.cuh`.
  SpEED extractors' old manual `-EIO` checks gone with host residual
  (ADR-1380). See branch fix/bughunt-cuda.
