# TransNet V2 shot-boundary detector (100-frame window)

`transnet_v2` — a *shot-change* detector that consumes a 100-frame
sliding window of small RGB thumbnails and emits one shot-boundary
probability per frame. The first half of the Wave 1 §2.4 content-
adaptive encoding pipeline (the second half — per-shot CRF prediction —
is **T6-3b**, a follow-up that consumes these per-frame probabilities
through the existing feature collector).

!!! note "Status: real upstream weights (T6-3a-followup)"
    As of [ADR-0261](../../adr/0261-transnet-v2-real-weights.md) the
    `model/tiny/transnet_v2.onnx` checkpoint ships verbatim trained weights
    from upstream
    [github.com/soCzech/TransNetV2](https://github.com/soCzech/TransNetV2)
    (Soucek & Lokoc 2020, MIT) wrapped in a thin NTCHW-input adapter. The
    original placeholder-only design is documented in
    [ADR-0223](../../adr/0223-transnet-v2-shot-detector.md).

## What the outputs mean

The extractor appends two per-frame features:

| Feature name | Type | Meaning |
| --- | --- | --- |
| `shot_boundary_probability` | float32 in `[0, 1]` | Sigmoid of the network's logit for the frame. ~0.0 = no cut, ~1.0 = the frame is the last of a shot. |
| `shot_boundary` | float32 ∈ `{0.0, 1.0}` | Binary flag thresholded at 0.5 against the probability. Drop-in for naive consumers. |

A `1.0` marks the **last frame of a shot**, the frame before the cut, as
upstream's `predictions_to_scenes()` reads the predictions.

Downstream consumers (the per-shot CRF predictor T6-3b, the FFmpeg
shot-cut filter shipping with T6-3b) bind to those exact strings.

| Probability | Interpretation |
| --- | --- |
| **~0.05** | No shot change — typical mid-shot frame. |
| **~0.50** | Detector uncertain — common during dissolve / fade transitions. |
| **~0.95** | High-confidence shot cut. |

## Shipped checkpoint

| Field | Value |
| --- | --- |
| Model name | `vmaf_tiny_transnet_v2_v1` |
| Location | `model/tiny/transnet_v2.onnx` |
| Size | ~30 MiB (real upstream weights, ~7.7M parameters in the published checkpoint plus the ColorHistograms branch) |
| ONNX opset | 17 |
| Input | `frames` — float32 `[1, 100, 3, 27, 48]` (100-frame stack of RGB thumbnails, NTCHW) |
| Output | `output_0` — float32 `[1, 100]` (per-frame logits before sigmoid); the extractor binds it by position |
| Input range | 0..255 per sample, as upstream's RGB frames |
| Smoke flag | `smoke: false` in registry — real shot detector |
| License | MIT (upstream `soCzech/TransNetV2`) |
| Upstream commit | `77498b8e4a6d61ed7c3d9bd56f4de2b29ab7f4db` |
| TF SavedModel parity | max-abs-diff `< 4e-6` over 3 random `[0..255]` input trials |

The sidecar JSON at `model/tiny/transnet_v2.json` carries the input /
output names plus `frame_window: 100`, `thumbnail_h: 27`,
`thumbnail_w: 48`, `boundary_threshold: 0.5` so downstream consumers
can validate the contract without parsing the ONNX graph. Fresh exports add
ADR-0661 `run_provenance` with the upstream SavedModel paths, wrapped
SavedModel scratch path, parsed exporter arguments, ONNX output, sidecar
output, and registry target.

## Wrapper layer (NTCHW adapter)

Upstream's TensorFlow SavedModel takes
`[batch, frames, height, width, channels]` (NTHWC) and returns two
outputs: `output_1` (single-frame shot logits) and `output_2`
(auxiliary "many_hot" output trained against fades / dissolves). The
fork's C-side extractor (ADR-0223) declared an NTCHW input
`[1, 100, 3, 27, 48]` and a single `[1, 100]` logits output. The
exporter `ai/scripts/export_transnet_v2.py` wraps the upstream
SavedModel in a `tf.Module` whose forward pass:

1. transposes inputs from NTCHW → NTHWC (axes `0,1,2,3,4` → `0,1,3,4,2`),
2. invokes `base.signatures['serving_default']` with the upstream input,
3. selects only `output_1`,
4. squeezes the trailing singleton dim so downstream sees `[1, 100]`.

After tf2onnx conversion, one rank-2 `UnsortedSegmentSum` node in
upstream's `ColorHistograms` branch is rewritten as an equivalent
`ScatterND` reduction='add' subgraph (standard ONNX 17 doesn't ship
`SegmentSum`, and `tf2onnx` lowers `UnsortedSegmentSum` to a rank-1-
only op). The rewrite is numerically identical (no learned params
involved); see `_replace_segmentsum` in `ai/scripts/export_transnet_v2.py`.

## Op allowlist update

[ADR-0261](../../adr/0261-transnet-v2-real-weights.md) extended
`core/src/dnn/op_allowlist.c` with six ops that
appear in the upstream TransNet V2 graph: `BitShift`, `GatherND`, `Pad`,
`Reciprocal`, `ReduceProd`, `ScatterND`. Each is a deterministic
standard ONNX op with bounded runtime cost (no control-flow, no host
allocation). Rationale + alternatives in
[ADR-0261](../../adr/0261-transnet-v2-real-weights.md).

## Frame window contract

The C extractor (`core/src/feature/transnet_v2.c`) reproduces upstream's
`predict_frames()`
([ADR-1527](../../adr/1527-transnet-v2-upstream-windows.md)):

1. Each `extract()` call resizes the luma plane to a 27x48 grid by nearest
   neighbour, keeps it in the 0..255 range (10- and 12-bit samples are scaled
   to it) and broadcasts it across the three RGB channels. It stores the
   thumbnail in a 100-slot ring keyed by frame index.
2. The clip is padded as upstream pads it: 25 copies of the first frame in
   front, copies of the last frame behind. Window k covers frames
   `50k - 25 .. 50k + 74` and runs as soon as frame `50k + 74` is read; its
   logits at slots 25..74 become the features of frames `50k .. 50k + 49`.
   The windows the clip's end leaves open (one or two) run in `flush()`.
3. Each logit goes through a sigmoid into `shot_boundary_probability`, and
   the 0.5 threshold gives `shot_boundary`.

Consequences:

- The network runs once per 50 frames, not once per frame.
- A frame's features are written up to 74 frames after it is read; read
  them after the run is flushed.
- Every frame sees at least 25 frames before and after it. A logit read from
  the window's last slot (the frame just read, which an earlier version of
  the extractor did) sees no later frame: on a hard cut between two natural
  clips it stayed at 0.10.
- The extractor is temporal: frames arrive in order from index 0, and a gap
  in the indices fails.

Two inputs differ from upstream: the luma plane stands in for RGB, and the
resize is nearest neighbour rather than ffmpeg's scaler. On a hard cut
between the Netflix `src01` clip and the BBB clip the extractor's
probability for the last frame before the cut is 0.89.

## Integration recipe

```bash
# 1. Build libvmaf with DNN support enabled.
meson setup core/build-cpu -Denable_dnn=enabled
ninja -C core/build-cpu

# 2. Run the extractor against a clip, supplying the model path.
core/build-cpu/tools/vmaf \
    --reference ref.yuv --distorted dis.yuv \
    --width 1920 --height 1080 --pixel_format 420 --bitdepth 8 \
    --feature transnet_v2=model_path=model/tiny/transnet_v2.onnx

# Or via env var (matches lpips_sq / fastdvdnet_pre):
VMAF_TRANSNET_V2_MODEL_PATH=model/tiny/transnet_v2.onnx \
    core/build-cpu/tools/vmaf --feature transnet_v2 ...
```

The extractor declines cleanly (non-fatal `-EINVAL`) if neither
`model_path` nor `VMAF_TRANSNET_V2_MODEL_PATH` is set, the same
contract as the LPIPS and FastDVDnet extractors.

## Reproducing the export

```bash
# 1. Fetch upstream weights (LFS-tracked ~30 MiB).
git clone --depth=1 https://github.com/soCzech/TransNetV2.git \
    /tmp/transnetv2_upstream
git -C /tmp/transnetv2_upstream lfs pull \
    -I inference/transnetv2-weights

# 2. Verify upstream sha256 (the exporter also enforces this; bumping
#    UPSTREAM_COMMIT in the script is a deliberate weights swap).
sha256sum /tmp/transnetv2_upstream/inference/transnetv2-weights/saved_model.pb
# expect: 8ac2a52c5719690d512805b6eaf5ce12097c1d8860b3d9de245dcbbc3100f554
sha256sum /tmp/transnetv2_upstream/inference/transnetv2-weights/variables/variables.data-00000-of-00001
# expect: b8c9dc3eb807583e6215cabee9ca61737b3eb1bceff68418b43bf71459669367

# 3. Install conversion deps in a Python 3.11 venv (TF doesn't yet
#    publish wheels for Python 3.14).
python3.11 -m venv /tmp/transnet-venv
/tmp/transnet-venv/bin/python -m pip install \
    tensorflow tf2onnx onnx onnxruntime numpy

# 4. Export.
/tmp/transnet-venv/bin/python ai/scripts/export_transnet_v2.py \
    --upstream-dir /tmp/transnetv2_upstream/inference/transnetv2-weights
```

The exporter overwrites `model/tiny/transnet_v2.onnx`,
`model/tiny/transnet_v2.json`, and the matching `model/tiny/registry.json`
row; it also asserts `< 1e-4` max-abs-diff against the wrapped TF
SavedModel before declaring success.

## Smoke test

The C-side registration + options-table contract + dual-feature
surface is exercised by `core/test/test_transnet_v2.c`, and the model end to
end (session open, the windows, the flush, 10-bit input) by
`core/test/dnn/test_transnet_v2_run.c` on synthetic clips with known cuts:

```bash
python3 scripts/ci/run_meson_test.py -- -C core/build test_transnet_v2 test_transnet_v2_run
```

To smoke the full 100-frame round-trip via Python ORT:

```bash
python3 -c "
import onnxruntime as ort, numpy as np
sess = ort.InferenceSession('model/tiny/transnet_v2.onnx',
                            providers=['CPUExecutionProvider'])
x = (np.random.RandomState(7).rand(1, 100, 3, 27, 48) * 255).astype(np.float32)
y = sess.run(['output_0'], {'frames': x})[0]
print('shape', y.shape, 'mean prob',
      float((1.0/(1.0+np.exp(-y))).mean()))
"
```

## Evaluation

The only recorded check is numerical parity: the exported ONNX matches the
upstream TF SavedModel to `< 4e-6` max-abs-diff on 3 random inputs (the exporter
asserts `< 1e-4`). Shot-boundary quality (F1 or precision/recall on a labelled
corpus) has not been measured in this repository, and none is hosted.

## Follow-ups

- **T6-3b**: per-shot CRF predictor consuming `shot_boundary_probability`
  per frame, plus shot-merge / min-length aggregation logic.
- **T6-3c**: switch the C-side resize from nearest-neighbour
  luma-broadcast to true bilinear RGB decode. Upstream was trained on
  bilinear-resized RGB, so the broadcast-luma path is a small loss
  of fidelity; quantifying it requires a labelled shot-boundary
  validation corpus we don't yet host.

## References

- Soucek, Lokoc. *TransNet V2: An effective deep network architecture
  for fast shot transition detection*, 2020.
  [arXiv:2008.04838](https://arxiv.org/abs/2008.04838).
- Reference implementation:
  [github.com/soCzech/TransNetV2](https://github.com/soCzech/TransNetV2)
  (MIT-licensed TensorFlow SavedModel).
- [ADR-0223](../../adr/0223-transnet-v2-shot-detector.md) — original
  design + placeholder-only PR.
- [ADR-0261](../../adr/0261-transnet-v2-real-weights.md) — this PR's
  decisions (NTCHW adapter, SegmentSum rewrite, op-allowlist
  extension).
- [Roadmap §2.4](../roadmap.md) — Wave 1 schedule.
- [ADR-0215](../../adr/0215-fastdvdnet-pre-filter.md) — sister
  placeholder-ONNX pattern (5-frame window FastDVDnet); its
  real-weights drop is
  [ADR-0255](../../adr/0255-fastdvdnet-pre-real-weights.md).
