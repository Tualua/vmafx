---
paths:
  - ai/pyproject.toml
  - ai/src/vmaf_train/__init__.py
  - ai/src/vmaf_train/cli.py
invariant: PyTorch + Lightning to ONNX training stack; vmaf-train CLI and local scripts are the operator surfaces.
---
<!-- markdownlint-disable MD013 MD060 -->
# Orientation: scope, layout, workflows

```text
ai/
  pyproject.toml   # package metadata (training-only deps)
  src/             # vmaf-train CLI + model defs + dataset loaders
  tests/           # pytest unit tests
  configs/         # dataset manifests + training recipes
  lpips_export.py  # re-export richzhang/PerceptualSimilarity → ONNX
```

## Governing ADRs

- [ADR-0020](../../docs/adr/0020-tinyai-four-capabilities.md) — four capabilities (C1–C4).
- [ADR-0021](../../docs/adr/0021-training-stack-pytorch-lightning.md) — PyTorch + Lightning training stack.
- [ADR-0023](../../docs/adr/0023-tinyai-user-surfaces.md) — `vmaf-train` CLI as one of four surfaces.
- [ADR-0036](../../docs/adr/0036-tinyai-wave1-scope-expansion.md) — Wave 1 scope (LPIPS, MobileSal, TransNet V2, …).
- [ADR-0039](../../docs/adr/0039-onnx-runtime-op-walk-registry.md) — runtime op-allowlist + registry schema.
- [ADR-0041](../../docs/adr/0041-lpips-sq-extractor.md) — LPIPS export pattern (ImageNet-in-graph).
- [ADR-0042](../../docs/adr/0042-tinyai-docs-required-per-pr.md) — doc-substance rule.

## Local workflow

```bash
pip install -e ai/
vmaf-train --help
vmaf-train register model/tiny/lpips_sq.onnx   # adds to registry.json
python ai/lpips_export.py                      # re-export LPIPS from the reference repo

# Netflix-corpus training (ADR-0203):
bash ai/scripts/run_training.sh
```
