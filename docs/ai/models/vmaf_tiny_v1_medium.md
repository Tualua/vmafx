# `vmaf_tiny_v1_medium` — legacy mlp_medium VMAF fusion regressor

> **Status — Superseded.** `vmaf_tiny_v1_medium` is retained for
> LOSO-eval baselines alongside `vmaf_tiny_v1`. For production use,
> prefer [`vmaf_tiny_v3`](vmaf_tiny_v3.md) (the `mlp_medium` successor)
> or later. See [ADR-0244](../../adr/0244-vmaf-tiny-v2.md).

`vmaf_tiny_v1_medium` is the medium-capacity sibling of
[`vmaf_tiny_v1`](vmaf_tiny_v1.md), trained under the same Phase-1
recipe over the canonical-6 features (`adm2`, `vif_scale0..3`,
`motion2`). It uses the `mlp_medium` architecture of the Phase-1 recipe (Linear
6→64→32→1
as shipped) without a baked-in `StandardScaler`. Both v1 variants are the
*single-split LOSO baselines* referenced in
[`docs/ai/loso-eval.md`](../loso-eval.md) — v1 covers the `mlp_small`
branch and v1_medium covers the `mlp_medium` branch for the capacity
comparison. `vmaf_tiny_v3` is the production `mlp_medium`-capacity
checkpoint; it supersedes v1_medium in all production contexts.

## Shipped checkpoint

| Field | Value |
| --- | --- |
| Model id | `vmaf_tiny_v1_medium` |
| Location | `model/tiny/vmaf_tiny_v1_medium.onnx` |
| Architecture | `mlp_medium` — Linear(6, 64) → ReLU → Linear(64, 32) → ReLU → Linear(32, 1), read from the ONNX initialisers |
| Trainable parameters | **2 561** |
| Input | `input` — float32 `[batch, 6]`, dynamic batch |
| Feature order | `adm2, vif_scale0, vif_scale1, vif_scale2, vif_scale3, motion2` |
| Output | `score` — float32 `[batch, 1]` |
| Ops | `Gemm`, `Relu` (no scaler in the graph) |
| Sidecar | none ships; only the ONNX file |
| ONNX opset | 17 |
| License | BSD-2-Clause-Patent |
| Registry entry | `vmaf_tiny_v1_medium` in `model/tiny/registry.json` (`"smoke": true`) |
| SHA-256 | `97f6116b44913f4076170a2f0cb78042db85aac7c56d432e14d9fe138ab952b7` |

## Training corpus

Identical to `vmaf_tiny_v1` — **Netflix Public Dataset** only (9 reference
sources; teacher score `vmaf_v0.6.1`). No KoNViD-1k or BVI-DVC rows.
`docs/ai/loso-eval.md` references both `vmaf_tiny_v1*.onnx` as
single-split LOSO baselines for the Phase-1 capacity sweep (small vs
medium).

## Validation

v1_medium has **not** been re-evaluated on the Phase-3 chain. For current
production PLCC / SROCC / RMSE numbers at `mlp_medium` capacity, see
[`vmaf_tiny_v3.md`](vmaf_tiny_v3.md) (trained on the 4-corpus union with
baked StandardScaler, ADR-0275 PTQ sidecar).

## When to use this model

| Use case | Recommendation |
| --- | --- |
| Production VMAF fusion (medium capacity) | Use [`vmaf_tiny_v3`](vmaf_tiny_v3.md) |
| LOSO capacity baseline (`mlp_medium`) | `vmaf_tiny_v1_medium` — required fixture |
| Accuracy comparison vs `mlp_small` | Pair with [`vmaf_tiny_v1`](vmaf_tiny_v1.md) |

## Runnable usage example

```bash
# Evaluate quality using the vmaf CLI with the medium-capacity tiny model:
vmaf \
    --reference python/test/resource/yuv/src01_hrc00_576x324.yuv \
    --distorted python/test/resource/yuv/src01_hrc01_576x324.yuv \
    --width 576 --height 324 --pixel_format 420 --bitdepth 8 \
    --model version=vmaf_v0.6.1 \
    --tiny-model model/tiny/vmaf_tiny_v1_medium.onnx \
    --json --output /tmp/vmaf_tiny_v1_medium.json
```

The tiny model is attached alongside the classic model and its score is added
under the feature name `vmaf_tiny_model` (there is no sidecar `name`).

!!! warning "The same run must compute the input features"
    A tiny feature-vector model reads its input features (`adm2`,
    `vif_scale0..3`, `motion2`) from the scores libvmaf computes in the same
    run, so keep `--model version=vmaf_v0.6.1` (it computes exactly these) or
    request them with `--feature adm --feature vif --feature motion`.
    A feature that is missing is read as `0.0` without a warning, and with only
    the default `vmaf_v1.0.16_3d0h` model the tiny model returns one constant
    value for every frame.

## Known limitations

- Trained on Netflix Public Dataset only — same limited UGC coverage as
  `vmaf_tiny_v1`; `vmaf_tiny_v3` uses the 4-corpus union.
- StandardScaler is **not** baked into the ONNX graph (Phase-1 recipe), and no
  sidecar carrying the scaler statistics ships. libvmaf therefore feeds the raw
  features, so scores from `--tiny-model` are not scaler-corrected: treat
  v1_medium as a fixture, not as a calibrated estimator.
- Superseded by `vmaf_tiny_v3` for both accuracy and corpus coverage.
  Do not use v1_medium in new pipelines.

## See also

- [`vmaf_tiny_v1.md`](vmaf_tiny_v1.md) — the `mlp_small` sibling; same
  Phase-1 recipe.
- [`vmaf_tiny_v3.md`](vmaf_tiny_v3.md) — the `mlp_medium` production
  successor (4-corpus union, baked scaler, ADR-0275 PTQ).
- [`docs/ai/loso-eval.md`](../loso-eval.md) — LOSO methodology; cites both
  v1 variants as single-split baselines.
- [ADR-0244](../../adr/0244-vmaf-tiny-v2.md) — v2 ship decision (also
  supersedes v1_medium as default).
- [ADR-0042](../../adr/0042-tinyai-docs-required-per-pr.md) — tiny-AI
  doc-substance rule this card satisfies.
