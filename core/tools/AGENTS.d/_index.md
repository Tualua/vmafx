# AGENTS.md — core/tools

Orientation for agents working on CLI binaries. Parent:
[../AGENTS.md](../../AGENTS.md).

## Scope

Three C binaries built by libvmaf's Meson tree:

- `vmaf` — end-user scoring CLI
- `vmaf_bench` — micro-benchmark harness for extractors and backends
- `vmaf-perShot` — per-shot CRF predictor sidecar (T6-3b / ADR-0222)
- `vmaf_roi` — saliency-driven ROI sidecar emitter for x265 / SVT-AV1 (T6-2b)

## Ground rules

- **Parent rules** apply (see [../AGENTS.md](../../AGENTS.md)).
- **No new hard dependencies** — CLI must still build when `enable_dnn=disabled`.
