<!-- markdownlint-disable MD013 MD041 MD060 -->
# ADR-1596: The SYCL VA-surface import runs on its own immediate-command-list queue

- **Status**: Accepted
- **Date**: 2026-10-02
- **Deciders**: lusoris
- **Tags**: `sycl`, `zero-copy`, `level-zero`, `correctness`, `driver-workaround`, `fork-local`

## Context

The zero-copy path of the `libvmaf_sycl` filter imports every decoder surface
with `zeMemAllocDevice` (DMA-BUF), copies or de-tiles its luma into the shared
frame slot, and frees the import with `zeMemFree` after the frame's queue wait
(`core/src/sycl/dmabuf_import.cpp`, ADR-1121). Until now that work ran on the
primary SYCL queue, which several extractors share.

On an Intel Arc A380 (i915, compute-runtime 26.35.39758.10, Level Zero 1.34.0)
with Unified Runtime's batched command lists
(`UR_L0_USE_IMMEDIATE_COMMANDLISTS=0`, the setting
[the bundling page](../backends/sycl/bundling.md) recommends for Arc A-series and
`scripts/test/sycl-dev-container.sh` forces), zero-copy `cambi` differed from
host upload in 3 to 7 of every 10 runs. Measured in the debug session
`sycl-zerocopy-cambi-nondeterminism`:

- From a random frame on, the import's writes into the shared slots stop
  landing; the slots keep two old frames and every extractor scores them.
  Canary kernels on the same queue are dropped as well. `wait_and_throw()`
  reports no error.
- The driver maps each new import at the GPU address the previous frame's import
  just freed. Delaying the free (new address every frame) or never freeing
  (a per-surface import cache) avoids the failure; waiting for every queue
  before the free does not.
- With immediate command lists (globally, or on the importing queue alone) the
  failure does not occur, with the same address reuse.

The defect sits in the driver stack (a submission is silently discarded), not in
libvmaf's synchronisation. An upstream report is drafted, not filed.

## Decision

We will run the VA-surface import (the device copy, the Tile4 / Y-tiled de-tile
and the P010 normalisation) on a dedicated in-order queue created with
`sycl::ext::intel::property::queue::immediate_command_list`, guarded by
`SYCL_EXT_INTEL_QUEUE_IMMEDIATE_COMMAND_LIST`. Every other queue keeps the
process's command-list mode, so the batched-mode advice for Arc A-series stays.
`vmaf_sycl_queue_wait` waits for the import queue before it frees imports, and
the extractors keep reaching the import through the existing de-tile event
barrier. Without the extension macro (another SYCL implementation) the queue is
a plain in-order queue; on a backend other than Level Zero the property has no
effect. The import queue stays immediate while the driver defect exists;
`zerocopy-e2e.sh --repeat 10` is the re-test.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Immediate import queue (chosen) | 0 divergent runs in every measured case; throughput within noise of HEAD | Relies on the driver honouring the property; one more queue | Chosen (D-08) |
| Make the whole primary queue immediate | Same result in the tests | Moves every extractor that shares the primary queue off the Arc A-series batched-mode setting | Changes more than the defect needs |
| Session cache of imports per surface | No address reuse, independent of the command-list mode | Every live import joins each submission's residency: +7 % ms per frame, -6 to -7 % fps at 1080p | Throughput loss above the phase budget |
| Delay each free by two frames | -2 to -4 % fps | Works only because the driver does not recycle the address yet; undocumented | Fragile |
| Stop recommending batched command lists | No code change | Gives up the Arc A-series workaround for every kernel | Rejected (D-08) |

## Consequences

- **Positive**: zero-copy scores are deterministic and equal host upload under
  both command-list modes.
- **Negative**: one extra queue per SYCL state; the workaround depends on the
  DPC++ extension and must be re-tested when the driver changes.
- **Neutral / follow-ups**: once compute-runtime fixes the drop, re-test with
  `--repeat 10` under `UR_L0_USE_IMMEDIATE_COMMANDLISTS=0` with the property
  removed before dropping the queue.

## References

- `req` (user, 2026-10-02), D-08: "option C' — only the VA import queue uses `sycl::ext::intel::property::queue::immediate_command_list`; the `UR_L0_USE_IMMEDIATE_COMMANDLISTS=0` advice for other kernels stays."
- [ADR-1121](1121-sycl-qsv-zerocopy-p010-normalization.md), [ADR-1595](1595-sycl-zerocopy-fail-loud-twin-routing.md).
- Testing: [docs/development/sycl-zerocopy-testing.md](../development/sycl-zerocopy-testing.md).
