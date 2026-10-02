---
paths:
  - core/src/dnn/ort_backend.c
  - core/src/dnn/ort_backend.h
  - core/include/libvmaf/dnn.h
invariant: Execution provider enums and CLI selectors append-only preserve device fallback chains across backends.
---
<!-- markdownlint-disable MD013 -->
# Hardware Execution Provider Wiring

- **CoreML EP wiring (ADR-0365, this PR)** — `VmafDnnDevice`
  values 5..8 (`COREML`, `COREML_ANE`, `COREML_GPU`, `COREML_CPU`)
  and `--tiny-device=coreml{,-ane,-gpu,-cpu}` CLI keywords are
  append-only. Wiring uses generic
  `SessionOptionsAppendExecutionProvider("CoreMLExecutionProvider", …)`
  form deliberately so Linux build needs no `coreml_provider_factory.h`
  conditional include; if future change switches to typed
  factory, also add `#if defined(__APPLE__)` guard around
  include and call site. `MLComputeUnits` key string values
  (`CPUAndNeuralEngine` / `CPUAndGPU` / `CPUOnly`) are part of
  CoreML EP public contract — never mutate them.
- **CoreML EP coexists with OpenVINO NPU EP (ADR-0332, draft PR
  \#496)**: both ADRs touch same enum, switch, and CLI grammar
  files. On rebase against either ADR's branch, conflicts are
  mechanical (adjacent enum values, adjacent switch cases, adjacent
  keyword strings). Keep enum values in append-only order
  (OpenVINO NPU/_CPU/_GPU = 5..7; CoreML = 5..8 — collision at 5..7
  resolved by whichever branch lands first taking 5..7, other
  taking 8..11). OpenVINO + CoreML AUTO-chain ordering
  (CUDA → OpenVINO-GPU → ROCm → CoreML → CPU) =
  ADR-0365-Decision-load-bearing.
- **OpenVINO NPU EP wiring (ADR-0332, 2026-05-08)** —
  `VmafDnnDevice` enum carries three explicit OpenVINO selectors
  (`OPENVINO_NPU` / `_CPU` / `_GPU`, values `5..7`) on top of
  generic `OPENVINO` (value `3`, GPU→CPU fallback chain).
  Explicit-selector branches in `ort_backend.c::vmaf_ort_open` pin
  `try_append_openvino()`'s `device_type` to `NPU` / `CPU` / `GPU`
  with **no** fallback inside branch. Two-stage CreateSession
  fallback to CPU EP is shared across all explicit-EP selectors,
  remains only safety net when requested OpenVINO device isn't
  present. NPU intentionally NOT in AUTO try-chain; opt-in only.
  `vmaf_dnn_session_attached_ep()` stable-string list gained
  `"OpenVINO:NPU"` — consumers asserting on returned string
  (documented in `docs/ai/inference.md` §Graceful EP fallback)
  must accept new value. End-to-end NPU silicon validation is
  deferred to contributor with Meteor / Lunar / Arrow Lake hardware.
