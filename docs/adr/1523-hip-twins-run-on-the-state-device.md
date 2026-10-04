<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1523: HIP twins run on the device of the imported state

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: Lusoris
- **Tags**: hip, gpu, api, fork-local

## Context

`vmaf_hip_context_new(&ctx, device_index)` stored `device_index` and selected
nothing, and every HIP twin called it with a literal `0`. A twin therefore ran
on whatever HIP device its thread had: `vmaf_hip_state_init()` calls
`hipSetDevice()` on the thread that creates the state, so the CLI, which scores
on that thread, got the device `--hip_device` names by accident, and a library
caller that scores on another thread got device 0. `vmaf_hip_device_count()`
returned 0 when `hipGetDeviceCount()` failed, so a broken runtime read as a
host without a device, and `vmaf_hip_list_devices()` skipped a device whose
properties could not be read and still counted it. The documentation audit
of 2026-10-03 recorded both as defect 15
(`T-HIP-DEVICE-INDEX-IGNORED-2026-10-04`).

HIP binds a device to a host thread (`hipSetDevice()`), not to a context
object the twins could carry, so honouring the index also needs every twin to
know which index to pass.

## Decision

`vmaf_hip_context_new()` checks `device_index` against
`vmaf_hip_device_count()` and selects it with `hipSetDevice()`: `-EINVAL`
outside `[0, count)`, `-ENODEV` with no device, the runtime's error otherwise.
libvmaf gives every extractor context the device of the imported state in a
new `VmafFeatureExtractor::hip_device_index` field (0 without a state, the
device `vmaf_hip_state_init()` picks for -1), and every HIP twin passes it,
directly or through its helper (CAMBI) or its pipeline configuration (SpEED).
libvmaf also calls `vmaf_hip_state_bind()` (`hipSetDevice()` on the state's
device) before a frame's HIP twins run and before the flush, so the thread
that calls `vmaf_read_pictures()` does not change the device.

`vmaf_hip_device_count()` returns the count when the runtime answers,
including 0 for `hipErrorNoDevice` (how ROCm reports an empty or masked
`HIP_VISIBLE_DEVICES` and a host without `/dev/kfd`, measured on ROCm 7.2.4),
and a negative errno for any other failure. `vmaf_hip_list_devices()` and
`vmaf_hip_state_init()` return the same errors instead of 0 or `-ENODEV`.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Pass the state's device through `fex->hip_device_index` and rebind per frame (chosen) | Each twin names its device explicitly; works from any calling thread; one int per extractor context | Touches every HIP twin's init and two libvmaf call sites | — |
| Keep the thread-current device: `vmaf_hip_context_new(ctx, -1)` meaning "current", twins pass -1 | No new extractor field | Still depends on which thread runs init; a library caller scoring on a worker thread lands on device 0 again | Leaves the defect in place for the case it hurt |
| Hand each twin the `VmafHipState *` itself | The twin could reach the stream as well | Exposes an opaque public type's internals to every twin; the state's stream is not used by the twins | More surface than one index needs |
| Report `hipErrorNoDevice` as `-ENODEV` from `vmaf_hip_device_count()` | Every non-success code is an errno | A host without a GPU, the hosted HIP lane included, would read as a runtime failure; callers that skip on `count <= 0` would treat a broken runtime and an empty host alike again | The runtime answered the question; 0 is the answer |

## Consequences

- **Positive**: `--hip_device N` and `VmafHipConfiguration.device_index` hold
  for every HIP twin and every calling thread; a broken HIP runtime is
  reported as an error by the count, the device list and the state.
- **Negative**: one more `hipSetDevice()` per frame and per flush when a HIP
  state is imported (a thread-local assignment in ROCm); the HIP twins' init
  now fails with `-EINVAL` when given a device the runtime does not have,
  where it used to run on the thread's device.
- **Neutral / follow-ups**: only one HIP device exists on the measuring host
  (a gfx1036), so selection of a device other than 0 is proven by the
  device-free `test_hip_device_selection` and the source contract
  `test_hip_device_index_contract`, not on two GPUs.

## References

- `.workingdir/evidence/docs-audit-2026-10-03/code-defects.md` item 15
  (local, not public).
- [ADR-0519](0519-hip-import-state-implementation.md) (HIP state import),
  [ADR-1408](1408-hip-shared-frame-planes.md) (the other per-context HIP field).
- Source: maintainer decision of 2026-10-04, "Track all, fix now".
