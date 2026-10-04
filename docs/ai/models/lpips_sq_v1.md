<!-- markdownlint-disable MD013 -->
# LPIPS-SqueezeNet v1 (`lpips_sq_v1`)

`lpips_sq_v1` (display name `vmaf_tiny_lpips_sq_v1`) — a full-reference
perceptual distance metric based on
the SqueezeNet variant of **LPIPS** (Learned Perceptual Image Patch
Similarity). It scores how *perceptually* different a distorted frame looks
from its reference, using features from a pretrained image classifier that
humans-in-the-loop were shown to agree with far better than MSE / PSNR on
distortions that matter for video quality (blocking, ringing, blur, banding).

LPIPS is the de-facto perceptual baseline in recent image/video-quality
literature. The fork ships SqueezeNet (not VGG or AlexNet) because it is ~70×
smaller (724k params, 3.2 MB) than the VGG backbone while retaining
competitive human correlation on the standard BAPPS benchmark. The family
page is [lpips_sq.md](lpips_sq.md).

## What the output means

The extractor emits a single feature named `lpips`, one value per frame pair.

| Value | Interpretation |
| --- | --- |
| **0.0** | Reference and distorted frames are perceptually identical |
| **~0.1** | Mild compression / small distortion — most viewers won't notice |
| **~0.3** | Visible but not obtrusive |
| **~0.6+** | Clearly degraded |
| **~1.0** | Saturated — distortion dominates |

LPIPS scores are **not linearly calibrated to MOS** — treat them as a
*ranking* signal across frames or across encodes, not as an absolute
quality number. For an MOS-calibrated score, combine with the classic VMAF
regressor (see [overview.md](../overview.md) capability C1).

## Shipped checkpoint

| Field | Value |
| --- | --- |
| Registry id | `lpips_sq_v1` |
| Display name (sidecar `name`) | `vmaf_tiny_lpips_sq_v1` |
| Location | `model/tiny/lpips_sq.onnx` |
| Size | 3.2 MB (3 268 579 bytes) |
| SHA-256 | `1402626680d5b69a793e647edda2c32f04e192f5cf1e7837bec8bde14187a261` |
| ONNX opset | 18 |
| Upstream source | [richzhang/PerceptualSimilarity](https://github.com/richzhang/PerceptualSimilarity) v0.1 (SqueezeNet linear weights) |
| License | BSD-2-Clause (upstream) |
| Exporter | `ai/lpips_export.py` |
| Registry entry | `lpips_sq_v1` in `model/tiny/registry.json` |
| Sidecar | `model/tiny/lpips_sq.json` |

The ONNX is deterministic (stripped `doc_string` / `metadata_props` /
`producer_version`) so the pinned sha256 stays stable across re-exports by
any engineer.

## Usage — CLI

```bash
vmaf \
    --reference ref.yuv \
    --distorted dist.yuv \
    --width 1920 --height 1080 --pixel_format 420 --bitdepth 8 \
    --feature lpips=model_path=model/tiny/lpips_sq.onnx \
    --output score.json
```

The `--feature` argument has the form `name=option=value`; join several
options with a colon (`name=opt1=v1:opt2=v2`). There is no
`--feature_params` option. The resulting JSON contains per-frame `lpips`
values under the `frames` array.

Alternatively, set the path via environment:

```bash
export VMAF_LPIPS_MODEL_PATH=model/tiny/lpips_sq.onnx
vmaf --reference ref.yuv --distorted dist.yuv \
     --width 1920 --height 1080 --pixel_format 420 --bitdepth 8 \
     --feature lpips --output score.json
```

Resolution order: `model_path` option > `VMAF_LPIPS_MODEL_PATH` env >
extractor declines `init()` with `-EINVAL`.

## Usage — C API

```c
#include <libvmaf/libvmaf.h>

VmafConfiguration cfg = { /* ... */ };
VmafContext *vmaf = NULL;
vmaf_init(&vmaf, cfg);

VmafFeatureDictionary *d = NULL;
vmaf_feature_dictionary_set(&d, "model_path", "model/tiny/lpips_sq.onnx");
vmaf_use_feature(vmaf, "lpips", d);

/* ... vmaf_read_pictures(...) / vmaf_read_pictures_mem(...) loop ... */

double lpips_mean = 0.0;
vmaf_feature_score_pooled(
    vmaf, "lpips", VMAF_POOL_METHOD_MEAN,
    &lpips_mean, /*index_low*/ 0, /*index_high*/ n_frames - 1);
```

## Usage — Python (libvmaf bindings)

```python
from vmaf.core.vmafexec_feature_extractor import VmafexecFeatureExtractor
from vmaf.tools.stats import ListStats

fex = VmafexecFeatureExtractor(
    logger=None, workdir_root=None,
    ref_path="ref.yuv", dis_path="dist.yuv",
    asset_dict={"width": 1920, "height": 1080,
                "yuv_type": "yuv420p",
                "quality_width": 1920, "quality_height": 1080},
    optional_dict={"feature_params": {"lpips": {
        "model_path": "model/tiny/lpips_sq.onnx",
    }}},
)
fex.run()
per_frame = fex.results[0].get_ordered_list_scores_key("lpips_scores")
print("mean LPIPS:", ListStats.nonemean(per_frame))
```

## Training and evaluation

The fork does not train or fine-tune this model. The weights are the
pretrained SqueezeNet backbone and linear calibration layers of upstream
`richzhang/PerceptualSimilarity` v0.1 (trained by its authors on the BAPPS
human-judgement dataset), exported to ONNX by `ai/lpips_export.py`. No
fork-side quality evaluation (correlation against MOS, or comparison with
VMAF and PSNR on a reference set) has been recorded; the C-side registration
and option contract is exercised by `core/test/test_lpips.c`.

## Known limitations

- **High-bit-depth input is RGB8-normalised.** The shipped checkpoint
  still consumes ImageNet-normalised RGB tensors, so 10/12/16-bit planar
  YUV inputs are rounded into the same 8-bit RGB tensor contract before
  ONNX inference.
- **BT.709 limited range.** Internal YUV→RGB conversion assumes BT.709
  studio-swing (`Y ∈ [16, 235]`, `UV ∈ [16, 240]`). BT.2020 / full-range
  sources will produce biased scores.
- **Nearest-neighbour chroma upsample.** We do not bilinear-upsample chroma
  for 4:2:0 input; this matches the deterministic reference pipeline of
  the upstream LPIPS evaluation but may diverge subtly from Netflix's
  bilinear path. Difference is typically <0.01 LPIPS; report cross-tool
  comparisons against the same pipeline.
- **Execution provider is chosen automatically.** The extractor opens its
  ONNX Runtime session with the default `auto` device, which tries CUDA,
  OpenVINO GPU, ROCm, CoreML and then CPU, whichever the linked ONNX Runtime
  provides. `--tiny-device` selects the provider for `--tiny-model` attaches,
  not for this extractor. The CPU path always works and is the reference.
- **No temporal smoothing.** Each frame is scored independently. For
  sequence-level quality, pool with `mean`/`harmonic_mean` at the
  `VmafContext` level.

## Re-exporting the ONNX

The shipped `lpips_sq.onnx` is produced by `ai/lpips_export.py`. To
regenerate it from the upstream weights:

```bash
# once: install the exporter's Python deps
.venv/bin/pip install torch torchvision lpips onnx onnxruntime

# export
.venv/bin/python ai/lpips_export.py \
    --out model/tiny/lpips_sq.onnx \
    --sidecar model/tiny/lpips_sq.json

# update the registry sha256 if anything changed (exporter prints it)
```

The exporter absorbs the *inverse*-ImageNet transform into the graph so
that the C side can feed it tensors from the shared
`vmaf_tensor_from_rgb_imagenet()` helper (used by every ImageNet-family
model — MobileSal, future MUSIQ, etc.). See
[ADR-0040](../../adr/0040-dnn-session-multi-input-api.md) and
[ADR-0041](../../adr/0041-lpips-sq-extractor.md) for the rationale.

## See also

- [lpips_sq.md](lpips_sq.md) — LPIPS-Sq family page
- [overview.md](../overview.md) — where LPIPS fits in the C1–C4 capability map
- [inference.md](../inference.md) — loading + using tiny models from libvmaf
- [security.md](../security.md) — ONNX op-allowlist + registry sha256 pinning
- [benchmarks.md](../benchmarks.md) — PLCC/SROCC/RMSE methodology (no LPIPS
  numbers are recorded there)
