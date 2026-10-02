<!-- markdownlint-disable MD013 MD060 -->
# AGENTS.md — ai/

Orientation for agents working on tiny-AI **training** side. Parent:
[../AGENTS.md](../../AGENTS.md).

## Scope

Python package for training, exporting, registering tiny-AI
checkpoints, consumed by [core/src/dnn/](../../core/src/dnn/AGENTS.md)
at runtime. Stack: PyTorch + Lightning → ONNX.

## Ground rules

- **Parent rules** apply: see [../AGENTS.md](../../AGENTS.md).
- **Boundary = `.onnx` + sidecar JSON on disk.** Training lives
  here, runtime lives in `core/src/dnn/`; two communicate only
  through files in `model/tiny/`. No imports cross this boundary.
- **Every shipped `.onnx` has registry entry** in
  [`../model/tiny/registry.json`](../../model/tiny/) with sha256,
  upstream source, license, and opset. See
  [ADR-0039](../../docs/adr/0039-onnx-runtime-op-walk-registry.md).
- **ONNX opset**: export requests opset 17 but torch dynamo may
  emit 18 (downconvert sometimes fails in
  `onnx.version_converter`). Record emitted opset in registry
  sidecar rather than failing export.
- **ImageNet normalisation lives in graph**, not in C helper. For
  any ImageNet-family model, absorb inverse ImageNet transform into
  exported graph so C side uses shared
  `vmaf_tensor_from_rgb_imagenet()` helper unchanged. See
  [ADR-0041](../../docs/adr/0041-lpips-sq-extractor.md).
- **Roundtrip-validate** every export against `onnxruntime` to
  atol=1e-5 before committing. See
  [ADR-0021](../../docs/adr/0021-training-stack-pytorch-lightning.md).
- **Docs**: every new model or training recipe ships page under
  `docs/ai/` in same PR. See
  [ADR-0042](../../docs/adr/0042-tinyai-docs-required-per-pr.md).
- **Package `__init__.py` carries `__all__`** — every fork-added
  Python package under `ai/` (top-level `ai/`, `ai/data/`,
  `ai/train/`, `ai/src/vmaf_train/`, `ai/src/vmaf_train/data/`, ...)
  declares `__all__` as machine-readable public-surface contract,
  plus module docstring enumerating sub-modules, plus Lusoris + SPDX
  header. For namespace packages, `__all__` lists sub-module names;
  for re-export packages it lists re-exported symbols; for
  `__version__`-only packages it's `["__version__"]`. See
  [ADR-0911](../../docs/adr/0911-init-py-export-completeness-audit.md).
