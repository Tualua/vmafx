---
paths:
  - ai/train/qat.py
  - ai/scripts/qat_train.py
  - ai/configs/*qat*.yaml
invariant: Two-step QAT: PyTorch QAT to fp32 ONNX then ORT static-quantize; torchao.quantization.pt2e recipe.
---
<!-- markdownlint-disable MD013 MD060 -->
# Quantization-Aware Training (ADR-0207 / ADR-0208)

QAT trainer hook lives in [`ai/train/qat.py`](../train/qat.py); CLI
driver in [`ai/scripts/qat_train.py`](../scripts/qat_train.py). Default
config example =
[`ai/configs/learned_filter_v1_qat.yaml`](../configs/learned_filter_v1_qat.yaml).

**Pipeline (per ADR-0207 + ADR-0208 implementation bridge):**

1. fp32 warm-start training.
2. Graph capture with `torch.export` + fake-quant insertion via
   `torchao.quantization.pt2e.prepare_qat_pt2e` under
   `X86InductorQuantizer`'s default recipe: per-tensor `uint8`
   activation, per-channel symmetric `int8` weight (ADR-1293).
3. QAT fine-tune at 10× reduced LR.
4. Copy QAT-conditioned weights into fresh fp32 module, export
   to ONNX (torch.export-based exporter; target is plain fp32,
   so legacy path is not needed), then ORT static-quantize with
   calibration set drawn from QAT distribution. Output =    QDQ `.int8.onnx`.

**Rebase-sensitive invariants:**

- Two-step pipeline (PyTorch QAT → fp32 ONNX → ORT
  static-quantize) is load-bearing. Do NOT collapse to
  `convert_fx → torch.onnx.export` — both PyTorch 2.11 ONNX
  exporters refuse `convert_fx` output (legacy emits
  `quantized::conv2d`; TorchDynamo trips on
  `Conv2dPackedParamsBase.__obj_flatten__`). Re-check on each
  PyTorch upgrade.
- State-dict transfer in `_copy_qat_weights_into_fp32` matches
  by submodule name + tensor shape. `torch.export` capture keeps
  original parameter names (measured: 20/20 tensors transfer
  on `LearnedFilter`), but models using top-level `nn.Sequential`
  still break this; `RuntimeError("0 tensors copied")` guard
  catches it.
- exported graph module rejects `.train()` / `.eval()` and
  needs torchao's `move_exported_model_to_train` / `_to_eval`.
  `_set_mode()` dispatches on `isinstance(module,
  torch.fx.GraphModule)` because `_qat_fine_tune` runs against
  both raw Lightning module and prepared graph. Do not
  reintroduce bare `.eval()` on QAT model.
- Graph capture runs on CPU (`torch.export` is flaky on CUDA
  buffers here); trainer migrates to CPU before
  `prepare_qat_pt2e` and back to accelerator afterwards.
- `torch.ao.quantization` is deprecated wholesale and raises   `DeprecationWarning` `filterwarnings = ["error"]` setting
  turns into test failure. QAT hook moved to
  `torchao.quantization.pt2e` in ADR-1293; do not restore
  `prepare_qat_fx` or `get_default_qat_qconfig_mapping` on   rebase. `torch.export.export_for_training` does not exist in
  torch 2.14 — use `torch.export.export(...).module()`.
- pt2e recipe keeps weight side byte-identical
  (`int8`, `per_channel_symmetric`, `ch_axis=0`, [-128, 127])
  and widens activation range from old mapping's
  reduce-range [0, 127] to [0, 255]. That matches ORT
  `quantize_static`, which bakes activation ranges that
  ship; narrower range was mismatch.
