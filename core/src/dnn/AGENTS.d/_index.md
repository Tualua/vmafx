<!-- markdownlint-disable MD013 -->
# AGENTS.md — core/src/dnn

Orientation for agents working on ONNX Runtime integration (tiny-AI
inference layer). Parent: [../../AGENTS.md](../../../AGENTS.md).

## Scope

C-side runtime for tiny-AI checkpoints. Sits between feature
extractors and ONNX Runtime.

```text
dnn/
  dnn_api.c / dnn_ctx.h    # public vmaf_dnn_* surface (opened from feature extractors)
  model_loader.c/.h        # loads model/tiny/registry.json, pins paths, checks sha256
  onnx_scan.c/.h           # wire-format scanner — walks ModelProto for banned ops
  op_allowlist.c/.h        # allowlist of ONNX ops we permit (no Scan, bounded Loop/If)
  ort_backend.c/.h         # thin wrapper over ONNX Runtime C API (session + tensors)
  tensor_io.c/.h           # tensor helpers (luma8, RGB + ImageNet normalisation)
  meson.build
```

Public API: [../../include/libvmaf/dnn.h](../../../include/libvmaf/dnn.h).
Feature-extractor side consumes this API; no feature code talks to ONNX
Runtime directly.

## Ground rules

- **Parent rules** apply in full (see [../../AGENTS.md](../../../AGENTS.md)).
- **Every tiny-AI change ships docs** under `docs/ai/` in same PR. See
  [ADR-0042](../../../../docs/adr/0042-tinyai-docs-required-per-pr.md).

## Governing ADRs

- [ADR-0020](../../../../docs/adr/0020-tinyai-four-capabilities.md) — four capabilities.
- [ADR-0022](../../../../docs/adr/0022-inference-runtime-onnx.md) — ORT runtime + execution-provider mapping.
- [ADR-0023](../../../../docs/adr/0023-tinyai-user-surfaces.md) — CLI / C API / ffmpeg / training surfaces.
- [ADR-0036](../../../../docs/adr/0036-tinyai-wave1-scope-expansion.md) — Wave 1 scope (LPIPS, MobileSal, TransNet V2, …).
- [ADR-0039](../../../../docs/adr/0039-onnx-runtime-op-walk-registry.md) — op-allowlist walk + registry schema.
- [ADR-0040](../../../../docs/adr/0040-dnn-session-multi-input-api.md) — multi-input/output API with named bindings.
- [ADR-0041](../../../../docs/adr/0041-lpips-sq-extractor.md) — LPIPS-SqueezeNet extractor + ImageNet-in-graph.
- [ADR-0042](../../../../docs/adr/0042-tinyai-docs-required-per-pr.md) — doc-substance rule.
- [ADR-0169](../../../../docs/adr/0169-onnx-allowlist-loop-if.md) +
  [ADR-0171](../../../../docs/adr/0171-bounded-loop-trip-count.md) —
  `Loop` + `If` admitted with bounded trip-count guard
  (`VMAF_DNN_MAX_LOOP_NODES = 16`); `Scan` stays rejected.
- [ADR-0258](../../../../docs/adr/0258-onnx-allowlist-resize.md) —
  `Resize` admitted for U-2-Net / mobilesal / saliency / segmentation
  models. Consumers shipping their own ONNX should keep
  `mode in ("nearest", "linear")` (`cubic` not exercised in-tree).
- [ADR-1089](../../../../docs/adr/1089-dnn-onnx-domain-bypass.md) —
  `NodeProto.domain` (field 7) validated in addition to `op_type`;
  only `""` and `"ai.onnx"` permitted (custom/vendor domains
  rejected). Closes `(domain, op_type)` tuple bypass.
- [ADR-0207](../../../../docs/adr/0207-tinyai-qat-design.md) +
  [ADR-0208](../../../../docs/adr/0208-learned-filter-v1-qat-impl.md)
  — QAT pipeline (PyTorch QAT → fp32 ONNX → ORT static-quantize
  bridge for PyTorch 2.11 ONNX-exporter limitations).

## Testing

```bash
python3 "$(git rev-parse --show-toplevel)/scripts/ci/run_meson_test.py" -- -C build --suite=dnn
```

Unit tests live under [../../test/dnn/](../../../test/dnn/). CI also runs
`--tiny-model` smoke gate loading generated 1KB `smoke_v0.onnx` through
full loader → scanner → session-open path.
