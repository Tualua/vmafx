<!-- markdownlint-disable MD013 MD060 -->
# vmaf_tiny_v4 — top-rung VMAF feature-fusion estimator (opt-in only)

`vmaf_tiny_v4` is a tiny multi-layer perceptron that predicts a VMAF
score from the same six classic libvmaf features as
[`vmaf_tiny_v2`](vmaf_tiny_v2.md) (`adm2`, `vif_scale0..3`, `motion2`
— the canonical-6 set used by `vmaf_v0.6.1`). It carries about 4x
the parameters of v3 (`mlp_large` = 6 → 64 → 32 → 16 → 1, 3 073 params vs
v3's 769) to answer the question raised in PR #294: does the next rung on the
architecture ladder buy further headroom over v3's PLCC = 0.9986 ± 0.0015?

!!! note "Empirical answer: no, the ladder saturates"
    v4 ships as an **opt-in-only** model. Its Netflix 9-fold LOSO PLCC is
    0.9987 ± 0.0015, statistically indistinguishable from v3 (+0.0001 mean,
    identical std). The production default stays `vmaf_tiny_v2`; the
    higher-tier opt-in stays v3. See
    [ADR-0390](../../adr/0390-vmaf-tiny-v4-mlp-large.md) for the "ladder stops
    here" rationale.

## What the output means

A single scalar per frame on the same 0–100 VMAF scale as the
classic SVM regressor and as v2 / v3. Identical interpretation table:

| Value | Interpretation |
| --- | --- |
| **100** | Perceptually identical to the reference |
| **80–95** | High-quality encode |
| **60–80** | Visible compression artifacts |
| **< 60** | Heavy degradation |

## Shipped checkpoint

| Field | Value |
| --- | --- |
| Model name | `vmaf_tiny_v4` |
| SHA-256 (fp32) | `2bccd339caa08f25433120546e45f2cb435288edeeefeafc56f3668986904e8f` |
| Location | `model/tiny/vmaf_tiny_v4.onnx` |
| Architecture | `mlp_large` — Linear(6,64) → ReLU → Linear(64,32) → ReLU → Linear(32,16) → ReLU → Linear(16,1), 3 073 params |
| Input | `features` — float32 `[N, 6]`, dynamic batch |
| Feature order | `adm2, vif_scale0, vif_scale1, vif_scale2, vif_scale3, motion2` |
| Output | `vmaf` — float32 `[N]` |
| ONNX opset | 17 |
| ONNX size | 14 046 bytes |
| Quantisation | fp32 + dynamic-PTQ int8 sidecar (ADR-0275) |
| License | BSD-2-Clause-Patent |
| Registry entry | `vmaf_tiny_v4` in `model/tiny/registry.json` |
| Sidecar | `model/tiny/vmaf_tiny_v4.json` |
| Exporter | `ai/scripts/export_vmaf_tiny_v4.py` |
| Trainer | `ai/scripts/train_vmaf_tiny_v4.py` |
| LOSO harness | `ai/scripts/eval_loso_vmaf_tiny_v4.py` |

The graph bakes the StandardScaler `(mean, std)` from the training
set as Constant nodes that run *before* the MLP — exactly the same
runtime contract as v2 / v3. There is no out-of-band scaler file
to ship.

Fresh exports add a `run_provenance` block to the sidecar. It records
the exporter entrypoint, CLI arguments, checkpoint input, and ONNX /
sidecar output paths so refreshed model artifacts can be traced without
reading local shell history.

Effective topology:

```text
features [N, 6]
   |
   Sub  <- mean   ([6] constant)
   |
   Div  <- std    ([6] constant)
   |
   Linear(6, 64)  -> ReLU
   Linear(64, 32) -> ReLU
   Linear(32, 16) -> ReLU
   Linear(16, 1)
   |
   Squeeze(axis=-1) -> vmaf [N]
```

## How to invoke

The CLI attaches v4 with `--tiny-model <path>`, alongside the classic models:

```bash
vmaf --reference ref.yuv --distorted dist.yuv \
     --width 1920 --height 1080 --pixel_format 420 --bitdepth 8 \
     --model version=vmaf_v0.6.1 \
     --tiny-model model/tiny/vmaf_tiny_v4.onnx
```

The score is added under the feature name `vmaf_tiny_model` (the sidecar has
no `name`).

!!! warning "The same run must compute the input features"
    A tiny feature-vector model reads its input features (`adm2`,
    `vif_scale0..3`, `motion2`) from the scores libvmaf computes in the same
    run, so keep `--model version=vmaf_v0.6.1` (it computes exactly these) or
    request them with `--feature adm --feature vif --feature motion`.
    A feature that is missing is read as `0.0` without a warning. With only the
    default `vmaf_v1.0.16_3d0h` model the scores are stored under
    option-suffixed
    names, so the lookup misses them and the tiny model returns one constant
    value for every frame.

The MCP `vmaf_score` tool exposes the flag as the `tiny_model` argument and
passes the string unchanged to `--tiny-model`, so it must be a path as well;
there is no registry-id lookup.

## Training recipe (reproducible)

```bash
# 1. Train (~40 s wall on 16-thread CPU, ~10 min CPU time).
python3 ai/scripts/train_vmaf_tiny_v4.py \
    --parquet runs/full_features_4corpus.parquet \
    --out-ckpt runs/vmaf_tiny_v4.pt \
    --out-stats runs/vmaf_tiny_v4_scaler.json

# 2. Export to ONNX with bundled scaler stats.
python3 ai/scripts/export_vmaf_tiny_v4.py \
    --ckpt runs/vmaf_tiny_v4.pt \
    --out-onnx model/tiny/vmaf_tiny_v4.onnx \
    --out-sidecar model/tiny/vmaf_tiny_v4.json

# 3. Smoke validate (PLCC >= 0.97 gate; v2 / v3 diff sanity check).
python3 ai/scripts/validate_vmaf_tiny_v4.py \
    --onnx model/tiny/vmaf_tiny_v4.onnx \
    --parquet runs/full_features_netflix.parquet \
    --rows 5000 --min-plcc 0.97 \
    --v2-onnx model/tiny/vmaf_tiny_v2.onnx \
    --v3-onnx model/tiny/vmaf_tiny_v3.onnx \
    --out-json runs/vmaf_tiny_v4_validate.json

# 4. 9-fold LOSO eval (~12 s wall total).
python3 ai/scripts/eval_loso_vmaf_tiny_v4.py \
    --parquet runs/full_features_netflix.parquet \
    --out-json runs/vmaf_tiny_v4_loso_metrics.json
```

The training stats JSON includes ADR-0661 `run_provenance` with the trainer
entrypoint, argv, parsed hyperparameters, parquet input, checkpoint target, and
stats target. Keep that block with any refreshed stats used for export.

The generated LOSO report includes `run_provenance` with the evaluation
entrypoint, original argv, parsed hyperparameters, feature parquet input, and
report target path. Treat that block as the run identity when comparing v4
refreshes or multi-run audit output.

The smoke-validation JSON from `--out-json` also carries ADR-0661
`run_provenance`, including the validated ONNX, parquet slice, optional v2/v3
comparison models, parsed gate arguments, and report path.

Hyperparameters (identical to v2 / v3 — only the architecture changes):

- Optimiser: Adam @ `lr=1e-3`.
- Loss: MSE.
- Epochs: 90.
- Batch size: 256.
- Seed: 0.
- Standardisation: corpus-wide mean / std on the training split, baked into
  ONNX.

## Validation results

### Netflix 9-fold LOSO (single seed, parity with v3 protocol)

| Source | n | PLCC | SROCC | RMSE |
| --- | ---: | ---: | ---: | ---: |
| BigBuckBunny  | 1500 | 0.9995 | 0.9964 | 0.86 |
| BirdsInCage   | 1440 | 0.9999 | 0.9998 | 0.30 |
| CrowdRun      | 1050 | 0.9999 | 0.9998 | 0.76 |
| ElFuente1     | 1260 | 0.9993 | 0.9952 | 1.13 |
| ElFuente2     | 1620 | 0.9989 | 0.9996 | 1.22 |
| FoxBird       |  900 | 0.9952 | 0.9955 | 2.82 |
| OldTownCross  | 1050 | 0.9992 | 0.9999 | 1.23 |
| Seeking       | 1500 | 0.9989 | 0.9961 | 1.63 |
| Tennis        |  720 | 0.9977 | 0.9978 | 1.15 |
| **Mean**      |      | **0.9987** | **0.9978** | **1.23** |
| **Std (n-1)** |      | **0.0015** | **0.0020** | **0.70** |

### Comparison vs v2 / v3

| Metric | v2 (mlp_small, 257) | v3 (mlp_medium, 769) | **v4 (mlp_large, 3 073)** |
| --- | ---: | ---: | ---: |
| NF LOSO mean PLCC  | 0.9978 (5-seed) | 0.9986 (1-seed) | **0.9987 (1-seed)** |
| NF LOSO std PLCC   | 0.0021          | 0.0015          | **0.0015** |
| NF LOSO mean SROCC | 0.9959          | 0.9977          | **0.9978** |
| 5000-row smoke PLCC| 0.9998          | 1.0000          | **1.0000** |
| Train-set RMSE     | 0.153           | 0.112           | **0.104** |
| ONNX size (bytes)  | 2 446           | 4 496           | **14 046** |
| Params             | 257             | 769             | **3 073** |

The +0.0001 mean PLCC delta v3 → v4 is well below 1 std of either model.
Train-set RMSE keeps improving (v4 over-fits a bit harder thanks to the ~4x
parameters), but the held-out PLCC has saturated.

## Why pick v4 (or not)

| Model | Params | ONNX bytes | LOSO PLCC (mean) | Pick it when |
| --- | ---: | ---: | ---: | --- |
| `vmaf_tiny_v2` | 257 | 2 446 | 0.9978 (5-seed) | You want the tightest bundle, lowest dispatch overhead and the cited Phase-3 baseline (production default) |
| `vmaf_tiny_v3` | 769 | 4 496 | 0.9986 (1-seed) | You want the higher-tier model with the smallest ONNX that still beats v2 measurably |
| `vmaf_tiny_v4` | 3 073 | 14 046 | 0.9987 (1-seed) | You want the top of the measured ladder, run CPU-only inference where ~14 KB is irrelevant, and want maximum train-set fidelity |

v4's win over v3 is below noise, so v3 is the usual opt-in choice.

## Quantisation (dynamic-PTQ int8 sidecar — ADR-0275)

A dynamic-PTQ int8 sibling lives at
`model/tiny/vmaf_tiny_v4.int8.onnx`, produced by
`ai/scripts/ptq_dynamic.py`. The runtime redirect in
`vmaf_dnn_session_open` (ADR-0174) loads the int8 sidecar when the
registry entry's `quant_mode != "fp32"`; the fp32 file stays on disk
as the regression baseline.

| Field | Value |
| --- | --- |
| Quant mode | `dynamic` (weights-only int8; activations quantised at runtime) |
| Sidecar file | `model/tiny/vmaf_tiny_v4.int8.onnx` |
| sha256 (int8) | `203a25905a3797b1cc3e3f347f4f6f491b491000d4424017beef64219767a9e9` |
| File size | 7 769 B int8 vs 14 046 B fp32 (×0.55) — 45 % smaller |
| PLCC drop (int8 vs fp32) | 0.000145 (Netflix features parquet, ~11k rows) |
| Budget | 0.01 (per ADR-0174 / ADR-0173 default) |
| CI gate | `ai-quant-accuracy` step in `Tiny AI` job |

`mlp_large` carries enough weight mass (3 073 params) that the int8
weight tensors actually drive the on-disk size. v4 is therefore the
first MLP-flavoured tiny model where dynamic PTQ delivers a
meaningful size win — v2 (~257 params) and v3 (~769 params) shrink
only marginally because their fp32 ONNX is dominated by op metadata
and Constant scaler nodes rather than weights.

```bash
# Reproduce
python ai/scripts/ptq_dynamic.py model/tiny/vmaf_tiny_v4.onnx
python ai/scripts/measure_quant_drop.py model/tiny/vmaf_tiny_v4.onnx
# -> [PASS] vmaf_tiny_v4   mode=dynamic PLCC=0.999855  drop=0.000145  budget=0.0100
```

## Limitations

- Same canonical-6 input contract as v2 / v3 — no new features. v4's quality
  ceiling is the canonical-6 information bottleneck, not its arch.
- Trained 4-corpus (NF Public + KoNViD + BVI-DVC A+B+C+D, 330 499 rows).
  Out-of-distribution content (HDR, 8K, screen content, animation outside the
  corpora) inherits the corpus's coverage limitations.
- Single-seed LOSO; v3 also single-seed. v2 was 5-seed. A multi-seed v4 LOSO
  study (5+ seeds) would tighten the variance estimate but is not gating; the
  saturation evidence is decisive enough.
- The architecture ladder **stops here**. Future tiny-VMAF gains require *regime
  change* (richer features, larger corpus, ensembles, distillation), not deeper
  / wider MLPs. See ADR-0390.

## Related

- [`vmaf_tiny_v2`](vmaf_tiny_v2.md) — production default.
- [`vmaf_tiny_v3`](vmaf_tiny_v3.md) — opt-in higher-tier, recommended for most
  opt-in uses.
- [ADR-0389](../../adr/0389-vmaf-tiny-v3-mlp-medium.md) — v3 ship + ladder
  candidate.
- [ADR-0390](../../adr/0390-vmaf-tiny-v4-mlp-large.md) — v4 ship + ladder stops
  here.
- [Research-0048](../../research/0048-vmaf-tiny-v4-mlp-large-evaluation.md) —
  full evaluation.
- [`docs/ai/inference.md`](../inference.md) — runtime / dispatch table.
