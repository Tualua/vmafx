# `smoke_multi_output_v0` — CI multi-output test fixture

> **Status — CI internal.** This is not a quality model and must not
> be used for video quality assessment. It exists solely as a multi-output
> graph probe for the libvmaf DNN integration CI gate.

`smoke_multi_output_v0` is a minimal ONNX graph that emits two separate named
output tensors (`mean_score` and `peak_score`). It is used to verify that the
C-side DNN loader (`dnn_attach_api.c`) and score collector correctly register
and record multiple named outputs from a single attached tiny model.

## Checkpoint facts

| Field | Value |
| --- | --- |
| Model id | `smoke_multi_output_v0` |
| Location | `model/tiny/smoke_multi_output_v0.onnx` |
| Architecture | `ReduceMean` and `ReduceMax` heads over the input — intentional CI probe (generator: `scripts/gen_multi_output_smoke_onnx.py`) |
| Trainable parameters | 0 (no weights) |
| Training | None: a generated graph with no weights |
| Input | `luma` — float32 `[1, 1, 4, 4]` |
| Output | `mean_score` (mean over the input) and `peak_score` (max over the input) — float32 scalars |
| ONNX opset | 17 |
| License | BSD-2-Clause-Patent |
| Registry entry | `smoke_multi_output_v0` in `model/tiny/registry.json` (`"smoke": true`) |
| SHA-256 | `e5f353d65d6766b9beac0e59ea586c419308c0e24a6316a7714b7a5e4aef30e9` |

## Purpose

The multi-output attach path in `vmaf_ctx_dnn_attach` allows tiny models to
record multiple per-frame metrics into the score dictionary under distinct
names. `smoke_multi_output_v0` verifies:

1. The companion sidecar (`model/tiny/smoke_multi_output_v0.json`) specifies
   `output_names: ["mean_score", "peak_score"]`.
2. The runtime attaches both output tensors and files per-frame values under
   their declared keys without memory leaks or name collisions.
3. Exercised in `core/test/dnn/test_vmaf_use_tiny_model.c` via
   `test_attached_multi_output_model_records_named_scores`.

## Output interpretation

Outputs are synthetic test signals: the mean and the maximum of the input
tensor. Values reflect the fixture, not perceptual quality. PLCC / SROCC / RMSE
are not applicable.

## Runnable usage example

```bash
# Verify via the C unit test suite:
python3 "$(git rev-parse --show-toplevel)/scripts/ci/run_meson_test.py" -- \
  -C build --suite=dnn test_vmaf_use_tiny_model

# Or inspect the session outputs via Python:
python3 -c 'import onnxruntime as ort; sess = ort.InferenceSession("model/tiny/smoke_multi_output_v0.onnx"); print("Outputs:", [o.name for o in sess.get_outputs()])'
```

## Known limitations

- **CI internal only**: do not use for perceptual quality assessment.
- **Fixed batch**: batch dimension is 1; batched scheduling is not supported.
- **CPU only**: test fixture is intended for fast CI validation.

## See also

- [`smoke_v0.md`](smoke_v0.md) — single-output CI smoke probe.
- [`core/test/dnn/test_vmaf_use_tiny_model.c`](../../../core/test/dnn/test_vmaf_use_tiny_model.c)
  — regression tests.
- [ADR-0042](../../adr/0042-tinyai-docs-required-per-pr.md) —
  tiny-AI documentation standard.
