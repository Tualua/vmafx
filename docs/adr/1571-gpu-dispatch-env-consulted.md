<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1571: a GPU dispatch variable is documented only when library code reads it; `VMAF_CUDA_DISPATCH` is read at extractor init, `VMAF_HIP_DISPATCH` and its predicate are removed

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: lusoris
- **Tags**: `cuda`, `hip`, `sycl`, `gpu`, `dispatch`, `docs`, `rc3`, `fork-local`

## Context

`docs/usage/env-vars.md` listed three per-feature dispatch variables, and the
backend pages described what each does. Reading the code on `origin/master`
`2889f963a`:

| Variable | Read by | Called from library code | Effect |
|---|---|---|---|
| `VMAF_SYCL_DISPATCH` (`direct` / `graph`) | `vmaf_sycl_select_strategy()` | `sycl/common.cpp::sycl_any_extractor_wants_graph()` | chooses graph replay or direct submission |
| `VMAF_CUDA_DISPATCH` (`direct` / `graph`) | `vmaf_cuda_select_strategy()` | nowhere (only `test_gpu_dispatch_runtime`) | none; the documented warning for `graph` never printed |
| `VMAF_HIP_DISPATCH` (`direct` / `none` / `disable`) | `vmaf_hip_dispatch_supports()` | nowhere (only `test_gpu_dispatch_runtime`) | none; the documented per-feature disable never happened |

The CUDA and SYCL variables pick a submission strategy, and the selectors
share one grammar ([ADR-0483](0483-gpu-dispatch-parse-dedup.md)). The HIP
variable is something else: a per-feature switch that would send a feature
back to the CPU. No other backend has such a switch; `--backend hip` picks a
twin by its `VMAF_FEATURE_EXTRACTOR_HIP` flag (`compute_fex_flags()` in
`libvmaf.c`, ADR-0530), and the HIP backend has one submission path.
`vmaf_hip_dispatch_supports()` came with a table of extractor and feature
names ([ADR-1154](1154-hip-backend-gaps.md)) that had to be kept in step with
`feature_extractor_list[]`, although nothing used its answer, and its header
was never installed, so no caller outside the library could probe it either.

## Decision

A `VMAF_*_DISPATCH` variable is documented only when library code reads it,
and `test_gpu_dispatch_env_contract.py` enforces that in both directions.
`VMAF_CUDA_DISPATCH` keeps its documented contract and becomes real:
`vmaf_feature_extractor_context_init()` calls `vmaf_cuda_select_strategy()`
for every CUDA extractor before its `init()`, keyed by the extractor's
registered name, so `vif_cuda:graph` logs that graph capture is not
implemented and the extractor runs direct; a strategy the backend cannot run
fails the initialisation with `-ENOSYS` rather than falling back.
`VMAF_HIP_DISPATCH`, `vmaf_hip_dispatch_supports()` and
`core/src/hip/dispatch_strategy.{c,h}` are removed with their docs.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Read `VMAF_CUDA_DISPATCH`, remove the HIP variable (chosen) | Every documented variable does what the page says; CUDA keeps the knob its graph-capture follow-up (ADR-0181) was meant to use; one registry decides HIP routing | CUDA reads a variable whose only visible effect today is a warning | Matches the documented CUDA contract and removes a HIP switch no other backend has |
| Wire `VMAF_HIP_DISPATCH` as a per-feature CPU fallback | The documented HIP behaviour would exist | A HIP-only routing switch, a second name table to keep in step with the registry, and behaviour CUDA and SYCL do not have | The brief asks for one behaviour across backends |
| Remove `VMAF_CUDA_DISPATCH` too | No variable that only warns | Drops a documented knob and the selector the CUDA graph-capture follow-up builds on | Not needed: reading it makes the page true |
| Keep both and correct the pages to say they have no effect | No code change | Two documented variables that do nothing | A user setting either learns nothing |

## Consequences

- **Positive**: setting `VMAF_CUDA_DISPATCH=<extractor>:graph` now reports
  that graph capture is unavailable; the HIP pages no longer describe a
  switch that does nothing; one fewer name table to maintain.
- **Neutral**: no score or routing changes: CUDA extractors ran direct before
  and do now, and HIP routing never consulted the removed predicate. Setting
  `VMAF_HIP_DISPATCH` had no effect before and has none now, so no migration
  is needed.
- **Follow-ups**: the CUDA graph-capture work (ADR-0181) returns a graph
  strategy from `vmaf_cuda_select_strategy()` only together with the code
  that runs it; until then the `-ENOSYS` guard refuses it.

## References

- Source: maintainer follow-up brief of 2026-10-04, lane CORE item 3
  (paraphrased): wire `VMAF_HIP_DISPATCH` the way the CUDA and SYCL dispatch
  variables work, or remove it with its docs if no other backend has the
  equivalent; decide from the code and say which.
- [ADR-0181](0181-feature-characteristics-registry.md),
  [ADR-0483](0483-gpu-dispatch-parse-dedup.md),
  [ADR-0530](0530-hip-feature-flag-promotion-and-picture-buffer.md),
  [ADR-1154](1154-hip-backend-gaps.md) (the HIP table and variable this
  removes).
