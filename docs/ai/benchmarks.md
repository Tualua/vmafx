<!-- markdownlint-disable MD060 -->
# Tiny AI — benchmarks

This page lists the accuracy of the shipped tiny-AI models and explains how to
produce comparable numbers for your own. The table is a registry snapshot of
model-card metrics. Regenerate the per-model reports before using any row for a
release claim.

!!! note
    Every full-reference number below was measured against the `vmaf_v0.6.1`
    teacher, before the one-shot retrain against `vmaf_v1.0.16_3d0h`. That
    retrain is RC9 work, see the [roadmap](../roadmap.md). Treat the figures as
    pre-retrain.

## Shipped-score snapshot

| Model | Target | Metric | Value | Gate | Note |
| --- | --- | --- | --- | --- | --- |
| `fr_regressor_v1` | FR VMAF-teacher score | Netflix Public Dataset 9-fold LOSO mean PLCC | `0.9982 ± 0.0014` | none | Tiny MLP over canonical-6 features; standardisation lives in the sidecar. |
| `fr_regressor_v2` | FR codec-aware VMAF-teacher score | Phase-A corpus in-sample PLCC | `0.9794` | promoted by ADR-0291's LOSO gate | Adds codec, preset and CRF conditioning. |
| `fr_regressor_v3` | FR codec-aware VMAF-teacher score | LOSO mean PLCC | `0.9975` | `>= 0.95` | Current 16-slot encoder-vocab model. |
| `vmaf_tiny_v2` | FR VMAF-teacher score | Netflix LOSO PLCC; KoNViD 5-fold PLCC | `0.9978 ± 0.0021`; `0.9998` | none | Recommended tiny fusion default; StandardScaler baked into the ONNX. |
| `vmaf_tiny_v3` | FR VMAF-teacher score | Netflix LOSO PLCC; train-set RMSE | `0.9986 ± 0.0015`; `0.112` | none | Higher-capacity opt-in model; int8 sidecar available. |
| `vmaf_tiny_v4` | FR VMAF-teacher score | Netflix LOSO PLCC | `0.9987 ± 0.0015` | none | Largest shipped tiny fusion model; opt-in. |
| `dists_sq_placeholder_v0` | FR perceptual-distance smoke | none claimed | none | none | Registry row is `smoke: true`; ABI / ORT two-input smoke checkpoint only. |
| `mobilesal_placeholder_v0` | NR saliency smoke | none claimed | none | none | Registry row is `smoke: true`; superseded for production ROI by `saliency_student_v1`; retained to preserve the historical MobileSal I/O contract. |

Runtime throughput depends on the ORT execution provider, CPU ISA and GPU
driver. Record measured CPU / CUDA / SYCL / OpenVINO numbers in the model card
or release note of the exact build under test, instead of one global table
here.

## Measure accuracy

For FR (C1) and NR (C2) models, three regression metrics are reported:

| Metric | Meaning |
| --- | --- |
| PLCC | Pearson linear correlation with MOS |
| SROCC | Spearman rank-order correlation |
| RMSE | root mean square error against MOS (0 to 100 scale) |

`vmaf-train eval` computes all three on the held-out test split produced by
`vmaf_train.data.splits.split_keys` with the fixed salt `vmaf-train-splits-v1`.
The splits are deterministic, so baseline and challenger see the same
frames and keys.

```bash
vmaf-train eval \
    --model model/tiny/vmaf_tiny_v2.onnx \
    --features ai/data/nflx_features.parquet \
    --split test
```

### Baseline: upstream `vmaf_v0.6.1` SVM

To compare a new tiny FR model against the upstream SVM, score the same test
pairs through both and run the helper functions in
`ai/tests/test_eval_metrics.py`. Keep the baseline's version in the committed
report for auditability.

## Measure runtime

`testdata/bench_all.sh` takes no arguments. It loops over the CPU, CUDA and SYCL
backends on three fixtures with the classic `vmaf_v0.6.1` model, and does not
time tiny models. Run it as the backend baseline:

```bash
bash testdata/bench_all.sh
```

To time a tiny model, run the CLI with the device you want to measure:

```bash
time vmaf -r ref.yuv -d dis.yuv -w 576 -h 324 -p 420 -b 8 \
     --model version=vmaf_v0.6.1 \
     --tiny-model model/tiny/vmaf_tiny_v2.onnx \
     --tiny-device cuda --threads 1
```

1. Run each configuration several times.
2. Report the median and p99 frames per second.
3. State the `--tiny-device` value and the EP that bound, see
   [inference.md](inference.md#graceful-ep-fallback).

The harness logs into `testdata/netflix_benchmark_results.json`. That file is an
ad-hoc run artefact and is never committed.

## Model-size targets

| Model class | Target size | Typical |
| --- | --- | --- |
| C1 (FR MLP) | ≤ 100 KB | ~50 KB |
| C2 (NR CNN) | ≤ 5 MB | ~2 MB |
| C3 (learned filter) | ≤ 2 MB | ~800 KB |

Models larger than `VMAF_DNN_DEFAULT_MAX_BYTES` (50 MB, a compile-time constant)
are rejected at load time. The historical `VMAF_MAX_MODEL_BYTES` environment
override was retired in T7-12: a tiny-AI model that grows past the targets means
the design is wrong, not the limit.

## Determinism in benchmarks

The same `--seed`, `train_commit` and dataset manifest SHA reproduce the
reported
scores within a tight `allclose`. CI includes a float-rounding guard, so a drift
of 1e-3 or more on the primary FR metric trips a regression failure.
