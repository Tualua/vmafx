# FR regressor v2 — codec-aware (vmaf-tune corpus consumer)

`fr_regressor_v2` — codec-conditioned successor to
[`fr_regressor_v1`](fr_regressor_v1.md). Maps a 6-D canonical libvmaf
feature vector plus a 14-D codec block to a VMAF teacher score.
Trained on the JSONL corpus emitted by `vmaf-tune corpus` (Phase A,
[ADR-0237](../../adr/0237-quality-aware-encode-automation.md)).

> **Status: production checkpoint.** `model/tiny/registry.json`
> registers `fr_regressor_v2.onnx` with `smoke: false`, SHA-256 pin
> `67934b0b61c73eb852d84ffb34e3333756e8da2530179ecc830336133e63e69e`,
> and an in-sample PLCC of 0.9794 on the vmaf-tune Phase-A JSONL
> corpus. The old scaffold-only card text is superseded; the follow-up
> line is now the v3 16-slot vocabulary / LOSO production checkpoint,
> documented in [`fr_regressor_v3.md`](fr_regressor_v3.md).

## Inputs

Two named tensors, dynamic batch axis:

- **`features`**, shape `(N, 6)` — canonical-6 libvmaf features
  (StandardScaler-normalised at training time using the mean/std
  baked into the sidecar JSON):

  | Index | Feature        |
  |-------|----------------|
  | 0     | `adm2`         |
  | 1     | `vif_scale0`   |
  | 2     | `vif_scale1`   |
  | 3     | `vif_scale2`   |
  | 4     | `vif_scale3`   |
  | 5     | `motion2`      |

- **`codec`**, shape `(N, 14)` — codec block (12 encoder one-hot slots,
  `preset_norm`, `crf_norm`), **not** normalised (already in `[0, 1]`):

  | Index | Slot                               |
  |-------|------------------------------------|
  | 0     | `encoder_onehot[libx264]`          |
  | 1     | `encoder_onehot[libx265]`          |
  | 2     | `encoder_onehot[libsvtav1]`        |
  | 3     | `encoder_onehot[libvvenc]`         |
  | 4     | `encoder_onehot[libvpx-vp9]`       |
  | 5     | `encoder_onehot[h264_nvenc]`       |
  | 6     | `encoder_onehot[hevc_nvenc]`       |
  | 7     | `encoder_onehot[av1_nvenc]`        |
  | 8     | `encoder_onehot[h264_qsv]`         |
  | 9     | `encoder_onehot[hevc_qsv]`         |
  | 10    | `encoder_onehot[av1_qsv]`          |
  | 11    | `encoder_onehot[unknown]`          |
  | 12    | `preset_norm`  (preset ordinal / 9)|
  | 13    | `crf_norm`     (CRF / 63)          |

  Encoder vocabulary (`encoder_vocab_version` 2) is closed and ordered —
  index 0..11 is load-bearing; bumping the vocabulary requires a re-train.
  The `unknown` bucket lets corpora without codec metadata pass an
  all-zeros + `unknown=1` vector and degrade gracefully.

  CRF normalised by **63** — the union upper bound across all
  supported encoders (libsvtav1 / libvpx-vp9 max). x264 / x265 use
  CRF up to 51; values above their per-encoder max are clipped at
  read time.

  Preset ordinal table per encoder lives in
  [`ai/scripts/train_fr_regressor_v2.py`](../../../ai/scripts/train_fr_regressor_v2.py)
  (`PRESET_ORDINAL`); the canonical 0..9 scale carries the
  speed-quality direction consistently across encoders. libsvtav1's
  numeric 0..13 presets are squashed to 0..9.

## Architecture

The shipped graph (read from the ONNX initialisers) is a GELU MLP over the
concatenated 20-D input (6 features + 14 codec values): three hidden layers
of 32 units, then a single output unit (about 2 820 parameters). The
trainer's own defaults are `--hidden 16 --depth 2`
(`ai/scripts/train_fr_regressor_v2.py`); the shipped checkpoint was exported
with a wider setting, so pass `--hidden 32 --depth 3` to reproduce its shape.

## Output

`score`, shape `(N,)` — a scalar VMAF-aligned quality score per
sample, same MOS range as v1 (typically `[0, 100]`).

## Codec-blind fallback

For inference paths that don't carry codec metadata, pass an
all-zeros codec vector with `encoder_onehot[unknown]=1` and
`preset_norm=0.5`, `crf_norm=0.5`. The model degrades to a v1-like
estimate; no graph surgery required.

## Training corpus

vmaf-tune Phase A JSONL (`tools/vmaf-tune/src/vmaftune/corpus.py`).
One row per `(source, encoder, preset, crf)` cell with
`schema_version=1`. The trainer reads the JSONL row-by-row; the
canonical-6 features come from each row's measured libvmaf feature
payload when present, with compatibility aliases for historical corpus
runs. `--smoke` remains available for CI/load-path validation, but the
committed `fr_regressor_v2.onnx` is the production export recorded in
the registry.

## CLI

```bash
# Smoke (synthetic corpus, validates the pipeline only)
python ai/scripts/train_fr_regressor_v2.py --smoke

# Production (real Phase A corpus)
python ai/scripts/train_fr_regressor_v2.py \
    --corpus runs/vmaf_tune_corpus.jsonl \
    --epochs 30
```

The script bakes the StandardScaler over the canonical-6 dims into
the sidecar JSON (`feature_mean` / `feature_std`); the codec block
is unscaled. Output ONNX is opset 17, dynamic batch axis, op-allowlist
checked.

The sidecar and `--metrics-out` JSON include `run_provenance`
(`ai-run-provenance-v1`): trainer entrypoint, command arguments,
real-corpus path/hash or `synthetic-smoke`, and output targets. Use this
block when comparing refreshed codec-aware runs so stale Phase-A
corpora are visible without reverse-engineering shell history.

## Checkpoint facts

| Field | Value |
| --- | --- |
| Model id | `fr_regressor_v2` |
| Location | `model/tiny/fr_regressor_v2.onnx` |
| Architecture | GELU MLP, 3 x 32 hidden units, codec conditioning block |
| Input | `features` `[N, 6]`, `codec` `[N, 14]` |
| Output | `score` `[N]` |
| ONNX opset | 17 |
| License | BSD-2-Clause-Patent |
| Registry entry | `fr_regressor_v2` in `model/tiny/registry.json` (`"smoke": false`) |
| SHA-256 | `67934b0b61c73eb852d84ffb34e3333756e8da2530179ecc830336133e63e69e` |

## Runnable usage example

```bash
# Score reference and distorted video using the codec-aware model:
vmaf \
    --reference python/test/resource/yuv/src01_hrc00_576x324.yuv \
    --distorted python/test/resource/yuv/src01_hrc01_576x324.yuv \
    --width 576 --height 324 --pixel_format 420 --bitdepth 8 \
    --model version=vmaf_v0.6.1 \
    --tiny-model model/tiny/fr_regressor_v2.onnx \
    --tiny-codec libx264 --tiny-preset medium --tiny-crf 28 \
    --json --output /tmp/fr_v2.json
```

The score is attached under the feature name `vmaf_tiny_model` (the sidecar has
no `name`). `--tiny-codec` takes an encoder from the sidecar `encoder_vocab`
(see Inputs); `--tiny-preset` and `--tiny-crf` set `preset_norm` and
`crf_norm`.

!!! warning "The same run must compute the input features"
    A tiny feature-vector model reads its input features (`adm2`,
    `vif_scale0..3`, `motion2`) from the scores libvmaf computes in the same
    run, so keep `--model version=vmaf_v0.6.1` (it computes exactly these) or
    request them with `--feature adm --feature vif --feature motion`.
    A feature that is missing is read as `0.0` without a warning. With only the
    default `vmaf_v1.0.16_3d0h` model the scores are stored under
    option-suffixed
    names, so the lookup misses them and the tiny model returns one constant
    value for every frame (measured on the CPU build with
    `fr_regressor_v1`: `-0.85`).

## Known limitations

- **Feature dependency**: requires the canonical-6 feature set
  (`adm2`, `vif_scale0..3`, `motion2`) extracted from 8-bit luma planes.
- **Closed encoder vocabulary**: supports `libx264`, `libx265`, `libsvtav1`,
  `libvvenc`, `libvpx-vp9`, `h264_nvenc`, `hevc_nvenc`, `av1_nvenc`,
  `h264_qsv`, `hevc_qsv`, `av1_qsv`, and `unknown` (fallback slot). The
  `vmaf --tiny-codec` option accepts exactly these names (plus common ffprobe
  aliases such as `h264` or `hevc`).
- **In-sample evaluation only**: the only recorded quality figure is the
  in-sample PLCC 0.9794 (SROCC 0.9640, RMSE 3.01 on 216 rows) in the sidecar's
  `training` block; there is no held-out or leave-one-source-out number for v2.
  The LOSO-gated successor is [`fr_regressor_v3`](fr_regressor_v3.md).
- **Execution providers**: validated on CPU (`CPUExecutionProvider`) and CUDA
  (`CUDAExecutionProvider`).
- **External data file**: uses external data format; companion
  `fr_regressor_v2.onnx.data` must remain co-located.

## See also

- [ADR-0272](../../adr/0272-fr-regressor-v2-codec-aware-scaffold.md)
  — original scaffold decision; this card now reflects the promoted
  production checkpoint.
- [ADR-0235](../../adr/0235-codec-aware-fr-regressor.md) — the
  parent codec-aware decision.
- [ADR-0237](../../adr/0237-quality-aware-encode-automation.md) —
  vmaf-tune Phase A (the corpus producer).
- [ADR-0249](../../adr/0249-fr-regressor-v1.md) —
  `fr_regressor_v1` baseline.
- [Research-0058](../../research/0058-fr-regressor-v2-feasibility.md)
  — feasibility digest, including the open question on production
  corpus diversity.
- [`fr_regressor_v2_codec_aware.md`](fr_regressor_v2_codec_aware.md)
  — superseded ADR-0235-era design card (canonical-9 / FULL_FEATURES
  path). The shipped model is `fr_regressor_v2` (this card), not a
  separate `fr_regressor_v2_codec_aware.onnx`.
