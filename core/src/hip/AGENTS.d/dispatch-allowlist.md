---
paths:
  - core/src/hip/dispatch_strategy.c
  - core/src/hip/dispatch_strategy.h
invariant: HIP dispatch allowlist uses exact public names and fail-closed override semantics under HAVE_HIPCC.
---
# HIP Dispatch Allowlist and Support Probing

## Rebase-sensitive invariants

- **HIP dispatch allowlist uses exact public names.** Under
  `HAVE_HIPCC`, `g_hip_features[]` must contain each active HIP extractor's
  `.name` and every `provided_features[]` key that callers may route through
  `vmaf_hip_dispatch_supports()`. Keep terminating `NULL` and
  fail-closed `direct` / `none` / `disable` environment override semantics.
  Adding extractor to `feature_extractor_list[]` without adding its exact
  dispatch names silently falls back to CPU. repository's
  `check-dispatch-registry.sh` guards global symbol registration only; review
  extractor's `provided_features[]` and extend relevant HIP runtime
  test when changing this table. Commit `53c8ef155` established this coupling.
