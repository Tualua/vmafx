# LPIPS-SqueezeNet (family page)

LPIPS-SqueezeNet is a full-reference perceptual distance: the `lpips` feature
extractor scores how different a distorted frame looks from its reference,
using features of a pretrained SqueezeNet. This page explains the family and
points at the versioned card; the usage, checkpoint facts and limitations
live on the card.

## Which card to read

| Registry id | Display name | File | Card |
| --- | --- | --- | --- |
| `lpips_sq_v1` | `vmaf_tiny_lpips_sq_v1` | `model/tiny/lpips_sq.onnx` | [lpips_sq_v1.md](lpips_sq_v1.md) |

The ONNX file is named `lpips_sq.onnx` (no version suffix), while the
registry id and the card carry the version. Future checkpoints get their own
versioned card next to `lpips_sq_v1.md`.

## At a glance

- **Extractor:** `lpips`, one value per frame pair; 0.0 means perceptually
  identical, larger means more different. The score is a ranking signal, not
  MOS-calibrated.
- **Run it:** `vmaf ... --feature lpips=model_path=model/tiny/lpips_sq.onnx`
  (or set `VMAF_LPIPS_MODEL_PATH`). The full CLI, C API and Python usage are
  on the [`lpips_sq_v1` card](lpips_sq_v1.md).
- **Upstream:**
  [richzhang/PerceptualSimilarity](https://github.com/richzhang/PerceptualSimilarity)
  v0.1 (SqueezeNet linear weights, BSD-2-Clause), exported by
  `ai/lpips_export.py`.
- **Size:** 3.2 MB, ONNX opset 18, two inputs (`ref`, `dist`), scalar output
  `score`.

## See also

- [overview.md](../overview.md) — where LPIPS fits in the C1–C4 capability map
- [inference.md](../inference.md) — loading and using tiny models from libvmaf
- [security.md](../security.md) — ONNX op-allowlist and registry sha256 pinning
- [ADR-0040](../../adr/0040-dnn-session-multi-input-api.md) and
  [ADR-0041](../../adr/0041-lpips-sq-extractor.md) — multi-input session API
  and the extractor design
