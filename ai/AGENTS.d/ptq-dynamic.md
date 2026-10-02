---
paths:
  - ai/scripts/ptq_dynamic.py
  - ai/scripts/measure_quant_drop.py
  - model/tiny/*.int8.onnx
invariant: Dynamic-PTQ int8 sidecars; measure_quant_drop gate requires drop < 1e-3 against fp32 baseline.
---
<!-- markdownlint-disable MD013 MD060 -->
# Dynamic-PTQ tiny-MLP family (ADR-0275)

`vmaf_tiny_v3` and `vmaf_tiny_v4` carry dynamic-PTQ int8 sidecars
produced by `ai/scripts/ptq_dynamic.py`. Recipe is identical to
`learned_filter_v1` (ADR-0174) and `nr_metric_v1` (ADR-0248): single
CLI invocation, no calibration data. On-disk size win is
proportional to weight mass — `mlp_large` (v4) shrinks 45 %,
`mlp_medium` (v3) shrinks 5 % because Constant scaler nodes and op
metadata dominate that graph. v2 (`mlp_small`) stays fp32: too
little weight mass for int8 sidecar to be worth audit cost.

**Invariants:**

- fp32 `<basename>.onnx` stays on disk as regression
  baseline; runtime redirect from ADR-0174 picks
  `.int8.onnx` sibling only when registry overlay declares
  `quant_mode != "fp32"`.
- `python ai/scripts/measure_quant_drop.py --all` = gate. Both
  v3 and v4 sit two orders of magnitude under 0.01 PLCC
  budget; treat any future drop > 1e-3 as regression worth
  investigating before merging int8 refresh.
- Re-running `ptq_dynamic.py` is deterministic on fixed fp32
  input — but sha256 of int8 output can shift across ORT
  versions. When ORT bumped, regenerate both sidecars and
  refresh `int8_sha256` in `model/tiny/registry.json` +
  `vmaf_tiny_v{3,4}.json` in same PR.
