---
paths:
  - core/test/test_cambi.c
  - core/test/test_cambi_stage_simd.c
  - core/test/test_integer_cambi_sycl.c
  - core/test/test_sycl_cambi_parity.c
invariant: test_integer_cambi_sycl.c is smoke test; test_sycl_cambi_parity.c is parity gate; both exist intentionally.
---
<!-- markdownlint-disable MD013 -->
# CAMBI regression seams and SYCL parity

- **CAMBI bounded-search regression seam**: `test_cambi.c` and
  `test_cambi_stage_simd.c` exercise internal helper routines via
  `feature/cambi_internal.h` linked against `libvmaf` (formerly unity-including
  `cambi.c`, resolved per CodeQL cpp/include-non-header alerts 1218 and 1241).
  Keep tests for TVI threshold/difference extremes, unreachable VLT
  threshold, and duplicate plus descending quick-select inputs. They pin
  termination bounds and partition ordering directly; end-to-end score alone
  cannot distinguish hang from numerically wrong search result.
- **`test_integer_cambi_sycl.c` is smoke test;
  `test_sycl_cambi_parity.c` is parity gate.** Both exist
  intentionally. Smoke test (ADR-0371) verifies registration +
  finite/non-negative output on flat frame. Parity test (ADR-1001,
  round 5) asserts headline `Cambi_feature_cambi_score` matches CPU
  path within places=4 on banding fixture. Do not merge two files —
  they serve different audit purposes. Do not remove
  `test_integer_cambi_sycl.c` in belief that parity test supersedes
  it; registration + format contract it pins is separate invariant.
  **Rebase-sensitive**: if `integer_cambi_sycl.cpp` gains new
  feature-name key or option, update both tests.
