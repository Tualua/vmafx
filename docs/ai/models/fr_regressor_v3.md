<!-- markdownlint-disable MD060 -->
# FR regressor v3 — codec-aware on ENCODER_VOCAB v3 (16-slot)

`fr_regressor_v3` — codec-aware FR regressor trained on
`ENCODER_VOCAB_V3` (16 slots). Parallel-shipped successor to
[`fr_regressor_v2`](fr_regressor_v2.md). Maps a 6-D canonical libvmaf
feature vector plus an 18-D codec block (16 encoder one-hot +
`preset_norm` + `crf_norm`) to a VMAF teacher score scalar.

!!! note "Status: production checkpoint (gate-passed)"
    - **Gate:** mean LOSO PLCC is **0.9975** across the 9 Netflix Public
      Dataset sources, above the
      [ADR-0302](../../adr/0302-encoder-vocab-v3-schema-expansion.md) ship
      gate of 0.95 (the gate
      [ADR-0291](../../adr/0291-fr-regressor-v2-prod-ship.md) cleared on v2).
    - **Registry:** ships under
      [ADR-0323](../../adr/0323-fr-regressor-v3-train-and-register.md); the
      registry row `fr_regressor_v3` has `smoke: false`.
    - **v2 stays authoritative for its slot:** the live
      `ENCODER_VOCAB_VERSION = 2` in
      [`ai/scripts/train_fr_regressor_v2.py`](../../../ai/scripts/train_fr_regressor_v2.py)
      remains authoritative for `fr_regressor_v2.onnx`. Promoting v3 to "the"
      canonical `fr_regressor_v2.onnx` slot is a separate follow-up PR (see
      ADR-0302 §Production-flip checklist).

## Inputs

Two named tensors, dynamic batch axis (matches the `vmaf_dnn_session_run`
two-input contract from
[ADR-0040](../../adr/0040-dnn-session-multi-input-api.md) /
[ADR-0022](../../adr/0022-inference-runtime-onnx.md)).

### `features`

Shape `(N, 6)`: canonical-6 libvmaf features, StandardScaler-normalised at
training time using the mean/std baked into the sidecar JSON
(`feature_mean`, `feature_std`):

| Index | Feature        |
|-------|----------------|
| 0     | `adm2`         |
| 1     | `vif_scale0`   |
| 2     | `vif_scale1`   |
| 3     | `vif_scale2`   |
| 4     | `vif_scale3`   |
| 5     | `motion2`      |

### `codec_block`

Shape `(N, 18)`: codec block, **not** normalised (already in `[0, 1]`):

| Index | Slot                                 |
|-------|--------------------------------------|
| 0     | `encoder_onehot[libx264]`            |
| 1     | `encoder_onehot[libaom-av1]`         |
| 2     | `encoder_onehot[libx265]`            |
| 3     | `encoder_onehot[h264_nvenc]`         |
| 4     | `encoder_onehot[hevc_nvenc]`         |
| 5     | `encoder_onehot[av1_nvenc]`          |
| 6     | `encoder_onehot[h264_amf]`           |
| 7     | `encoder_onehot[hevc_amf]`           |
| 8     | `encoder_onehot[av1_amf]`            |
| 9     | `encoder_onehot[h264_qsv]`           |
| 10    | `encoder_onehot[hevc_qsv]`           |
| 11    | `encoder_onehot[av1_qsv]`            |
| 12    | `encoder_onehot[libvvenc]`           |
| 13    | `encoder_onehot[libsvtav1]`          |
| 14    | `encoder_onehot[h264_videotoolbox]`  |
| 15    | `encoder_onehot[hevc_videotoolbox]`  |
| 16    | `preset_norm`  (0.5 on every row)    |
| 17    | `crf_norm`     ((CRF - 19) / 18)     |

The two scalar slots are not v2's. The trainer (`_build_codec_block()` in
`ai/scripts/train_fr_regressor_v3.py`) sets `preset_norm` to 0.5 on every
row and min-max normalises the CRF over its corpus. The shipped checkpoint's
corpus (`corpus_sha256` `58512e6c...`) spans CQ 19 to 37: the ensemble
trainer records that range for the same file, and both trainers compute it
with the same min and max. The sidecar declares the encoding
(`codec_preset_norm: constant`, `codec_preset_value: 0.5`,
`codec_crf_norm: minmax`, `codec_crf_min: 19`, `codec_crf_max: 37`), and
libvmaf fills the block that way
([ADR-1558](../../adr/1558-codec-block-encoding-from-sidecar.md)): with
`--tiny-crf 28` the slot is 0.5, `--tiny-preset` has no effect (the run says
so in a warning), and a CRF outside 19..37 gives a value outside [0, 1], as
the trainer's formula would. Before 2026-10-04 libvmaf filled v2's ordinal
preset / 9 and CRF / 63 here, inputs the model was not trained on.

The encoder vocabulary is closed and order-stable per
[ADR-0235](../../adr/0235-codec-aware-fr-regressor.md): the index of each
codec is the one-hot column index baked into the trained ONNX.

### Differences from v2

| Aspect | `fr_regressor_v2` | `fr_regressor_v3` |
|--------|-------------------|-------------------|
| Codec block | `(N, 14)`: 12 encoder slots + 2 | `(N, 18)`: 16 encoder slots + 2 |
| Slot order | `ENCODER_VOCAB` v2 (`libx264`, `libx265`, `libsvtav1`, ...) | `ENCODER_VOCAB_V3` (the 13 slots of the ADR-0291 layout, then `libsvtav1`, `h264_videotoolbox`, `hevc_videotoolbox` appended at 13, 14, 15) |
| `unknown` slot | Yes (slot 11, fallback for novel codecs) | No: the closed 16-slot vocabulary covers every adapter registered under `tools/vmaf-tune/src/vmaftune/codec_adapters/` |
| Output name | `score` | `vmaf` (matches the teacher-score column of the corpus rows; sidecar `output_names: ["vmaf"]`) |
| Input name of the codec tensor | `codec` | `codec_block` |

The v3 slot order follows the layout documented in ADR-0291 and kept as
`ENCODER_VOCAB_V3` since PR #401 (ADR-0302 scaffold); it is not the column
order of the shipped v2 sidecar (for example `libx265` is index 1 in v2 and 2
in v3).

!!! note
    The `vmaf` CLI validates `--tiny-codec` against the loaded model's
    sidecar `encoder_vocab`, so the v3 names (`libaom-av1`, `h264_amf`,
    `hevc_videotoolbox`, ...) are accepted with the v3 model even though
    the `--help` text lists the v2 names. A name not in the sidecar vocabulary
    is rejected. Because v3 has no `unknown` slot, always pass an encoder
    name that is in its vocabulary. The Python-side convention for callers
    without codec metadata is slot 0 (`libx264`), see "Codec-blind fallback".

## Output

`vmaf`, shape `(N,)` — a scalar VMAF-aligned quality score per sample,
same MOS range as v1/v2 (typically `[0, 100]`).

## Training corpus

Two corpus shapes are accepted, mapped to the same internal feature /
codec-block tensors at load time:

1. **`vmaf-tune` corpus, schema v3** (preferred,
   [ADR-0366](../../adr/0366-corpus-schema-v3.md)). One row per
   (source, encoder, preset, crf) encode, canonical-6 means /
   stddevs computed from libvmaf's `pooled_metrics` block:

   ```json
   {"schema_version": 3, "src": "BigBuckBunny_25fps.yuv",
    "encoder": "h264_nvenc", "preset": "p4", "crf": 19,
    "vmaf_score": 95.86,
    "adm2_mean": 0.99, "vif_scale0_mean": 0.88,
    "vif_scale1_mean": 0.99, "vif_scale2_mean": 0.996,
    "vif_scale3_mean": 0.998, "motion2_mean": 0.0,
    "adm2_std": 0.01, "vif_scale0_std": 0.02, ...}
   ```

   Rows with NaN canonical-6 means (libvmaf did not expose the
   feature, or the encode failed) are dropped before the
   StandardScaler is fitted — never imputed to 0.0. Legacy v2 corpora
   that carry only `vmaf_score` raise `ValueError` and point operators
   at this ADR; they cannot train this regressor.

2. **`hw_encoder_corpus.py` per-frame corpus** (legacy / NVENC-only).
   `runs/phase_a/full_grid/per_frame_canonical6.jsonl` (5,640 rows).
   One row per frame, bare canonical-6 column names, target column
   `vmaf`, quality knob `cq`. The training cohort the gate-passing
   v3 checkpoint was fit on.

### NVENC-only corpus caveat

!!! warning
    The current Phase A corpus drop is **NVENC-only** (slot 3,
    `h264_nvenc`). The remaining 15 vocabulary slots received **zero
    training examples** in this checkpoint.

Consequences for inference at the untrained slots:

- The MLP weights for the 15 unused one-hot columns remain at their Glorot
  initialisation. The signal for those codecs comes through the
  canonical-6 features, `preset_norm` and `crf_norm`.
- Predictions are **degraded but not random**: the canonical-6 features alone
  clear ~0.99 PLCC on the v1 single-input baseline
  ([ADR-0249](../../adr/0249-fr-regressor-v1.md)), so untrained-codec
  predictions inherit that baseline behaviour modulo the small one-hot column
  shift.
- The [ADR-0235](../../adr/0235-codec-aware-fr-regressor.md) multi-codec lift
  floor (at least +0.005 PLCC over v1) is **not yet measured**: the NVENC-only
  corpus does not exercise other codecs, so v3's lift over v1 reduces to v1
  vs v1 on NVENC.

v3 ships as the production graph regardless: it is forward-compatible with
the broader 16-slot schema, and re-using v2 would block multi-codec follow-up
corpora. The lift floor will be enforced retroactively when a future Phase A
corpus drop covers at least 3 codec families.

This caveat is the dominant reason the live `ENCODER_VOCAB_VERSION` stays at
2 in `train_fr_regressor_v2.py`: `fr_regressor_v2.onnx` remains the production
graph for cross-codec inference, and v3 is a parallel checkpoint that wins on
NVENC-specific predictions and serves as the schema-flip dry-run.

## Codec-blind fallback

For inference paths that don't carry codec metadata, pass an
all-zeros codec block with `encoder_onehot[libx264]=1` (slot 0 is the
fork's "default" canonical SW encoder), `preset_norm=0.5`,
`crf_norm=0.5`. The model degrades to a v1-like estimate; no graph
surgery required. Through libvmaf that is `--tiny-codec libx264 --tiny-crf 28`;
the vocabulary has no `unknown` entry, so libvmaf does not choose an encoder
for you.

## Training recipe

Identical to
[`fr_regressor_v2`](fr_regressor_v2.md) and the deep-ensemble LOSO
trainer
([ADR-0319](../../adr/0319-ensemble-loso-trainer-real-impl.md)):

- 9-fold leave-one-source-out (LOSO) over the unique `src` values.
- Per-fold StandardScaler fit on the training rows only (mirrors
  `eval_loso_vmaf_tiny_v3.py`).
- `FRRegressor(in_features=6, hidden=64, depth=2, dropout=0.1,
  num_codecs=18)`.
- Adam(`lr=5e-4`, `weight_decay=1e-5`), MSE loss, batch_size=32,
  200 epochs.
- Final ship checkpoint is fit on the **entire** corpus (no held-out
  split) once the LOSO gate passes — the LOSO fold is the gate, not
  the ship checkpoint.

## Headline results

Mean LOSO PLCC **0.9975** ± 0.0018 (n = 9 sources). Per-source PLCC:

| Source                    | PLCC   | SROCC  | RMSE  |
|---------------------------|--------|--------|-------|
| BigBuckBunny_25fps        | 0.9973 | 0.9878 | 0.787 |
| BirdsInCage_30fps         | 0.9988 | 0.9989 | 0.432 |
| CrowdRun_25fps            | 0.9996 | 0.9972 | 0.677 |
| ElFuente1_30fps           | 0.9987 | 0.8805 | 0.822 |
| ElFuente2_30fps           | 0.9950 | 0.9984 | 3.288 |
| FoxBird_25fps             | 0.9945 | 0.9329 | 0.904 |
| OldTownCross_25fps        | 0.9981 | 0.9951 | 0.810 |
| Seeking_25fps             | 0.9989 | 0.9877 | 1.013 |
| Tennis_24fps              | 0.9962 | 0.9436 | 1.061 |

Every source clears the relaxed per-source PLCC floor (0.85) from
[Research-0078](../../research/0078-encoder-vocab-v3-schema-expansion.md)
§Retrain ship gate criterion 3, and the mean clears the 0.95 hard
floor with ~5 percentage points of margin. The min/max spread
(0.9945 → 0.9996) is well under the 0.005 ensemble-spread bound from
ADR-0303.

## CLI

```bash
# Production (real Phase A corpus)
python ai/scripts/train_fr_regressor_v3.py \
    --corpus runs/phase_a/full_grid/per_frame_canonical6.jsonl

# Smoke (synthetic corpus, validates the pipeline only)
python ai/scripts/train_fr_regressor_v3.py --smoke
```

The script bakes the full-corpus StandardScaler over the canonical-6
dims into the sidecar JSON (`feature_mean` / `feature_std`); the
codec block is unscaled. Output ONNX is opset 17, dynamic batch axis,
op-allowlist checked. Smoke mode skips the ship gate; real-corpus
mode exits non-zero on gate-fail.

The sidecar includes `run_provenance` (`ai-run-provenance-v1`) with the
trainer entrypoint, parsed arguments, corpus path/hash, and output
targets. Smoke runs point at the generated temporary corpus, which makes
the sidecar explicit that the output is a pipeline check rather than a
real Phase-A training result.

## Checkpoint facts

| Field | Value |
| --- | --- |
| Model id | `fr_regressor_v3` |
| Location | `model/tiny/fr_regressor_v3.onnx` |
| Architecture | MLP with 16-slot codec conditioning block |
| Input | `features` `[N, 6]`, `codec_block` `[N, 18]` |
| Output | `vmaf` `[N]` |
| ONNX opset | 17 |
| License | BSD-2-Clause-Patent |
| Registry entry | `fr_regressor_v3` in `model/tiny/registry.json` (`"smoke": false`) |
| SHA-256 | `eaa16d23461eda74940b2ed590edfcaf13428aade294e47792a5a15f4d3b999c` |

## Runnable usage example

```bash
# Evaluate quality using the vmaf CLI with the v3 codec-aware model:
vmaf \
    --reference python/test/resource/yuv/src01_hrc00_576x324.yuv \
    --distorted python/test/resource/yuv/src01_hrc01_576x324.yuv \
    --width 576 --height 324 --pixel_format 420 --bitdepth 8 \
    --tiny-model model/tiny/fr_regressor_v3.onnx \
    --tiny-codec libx264 --tiny-preset medium --tiny-crf 28 \
    --json --output /tmp/fr_v3.json
```

The score is attached under the feature name `vmaf_tiny_model` (the sidecar has
no `name`).

!!! note "Input features and codec context"
    Loading the model makes the run compute its input features (`adm2`,
    `vif_scale0..3`, `motion2`, with default options), and the model scores
    every frame once the run is flushed. A frame without one of them fails the
    run with a message naming it; no input is read as `0.0`. The model also
    needs `--tiny-codec` (with the encode's `--tiny-preset` and `--tiny-crf`):
    without it the run stops on the first frame instead of scoring a guessed
    codec block
    ([ADR-1520](../../adr/1520-tiny-model-feature-inputs-at-flush.md)).

## Known limitations

- **Feature dependency**: requires the canonical-6 feature set
  (`adm2`, `vif_scale0..3`, `motion2`) extracted from 8-bit luma planes.
- **Closed 16-slot vocabulary**: the model has no `unknown` slot. The
  `vmaf` CLI rejects an encoder name that is not in the sidecar vocabulary;
  callers that build the codec block themselves use slot 0 (`libx264`) as
  their fallback convention.
- **Execution providers**: validated on CPU (`CPUExecutionProvider`) and CUDA
  (`CUDAExecutionProvider`).

## See also

- [ADR-0323](../../adr/0323-fr-regressor-v3-train-and-register.md) —
  this PR's decision record.
- [ADR-0302](../../adr/0302-encoder-vocab-v3-schema-expansion.md) —
  the v3 16-slot schema scaffold + ship gate definition.
- [ADR-0291](../../adr/0291-fr-regressor-v2-prod-ship.md) — v2
  production-flip; defines the 0.95 LOSO PLCC ship gate v3 reuses.
- [ADR-0235](../../adr/0235-codec-aware-fr-regressor.md) — the
  parent codec-aware decision; ≥+0.005 PLCC multi-codec lift floor.
- [ADR-0319](../../adr/0319-ensemble-loso-trainer-real-impl.md) —
  LOSO trainer pattern this script reuses.
- [Research-0078](../../research/0078-encoder-vocab-v3-schema-expansion.md)
  — schema expansion plan + retrain checklist.
- [`fr_regressor_v2`](fr_regressor_v2.md) — v2 model card; v3 is the
  parallel-shipped successor on the 16-slot vocab.
