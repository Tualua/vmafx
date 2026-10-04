<!-- markdownlint-disable MD013 -->
# Tiny AI — overview

Tiny AI lets you ship small, specialized perceptual-quality models next to the
classic VMAF SVM, without a second ML runtime and without giving up libvmaf's
C-only deployment. The feature is gated on `-Denable_dnn=auto|enabled|disabled`
(default `auto`) and consumed through one public header, `libvmaf/dnn.h`.

New here? Start with [the index](index.md), which shows the runnable path in
one command.

## The four capabilities

| # | Name | Shape | What you do with it | Shipped model |
| --- | --- | --- | --- | --- |
| **C1** | FR regressor | feature-vector → MOS | Replace or augment the upstream `vmaf_v0.6.1` SVM with a lightweight MLP trained on your own reference dataset. | [`vmaf_tiny_v2`](models/vmaf_tiny_v2.md), [`vmaf_tiny_v3`](models/vmaf_tiny_v3.md), [`vmaf_tiny_v4`](models/vmaf_tiny_v4.md), [`fr_regressor_v1`](models/fr_regressor_v1.md) to [`v3`](models/fr_regressor_v3.md) |
| **C2** | NR metric | frame → MOS | Predict quality without a reference (live encodes, consumer telemetry). | [`nr_metric_v1`](models/nr_metric_v1.md) |
| **C3** | Learned filter | frame → frame | Denoise, deblock or sharpen before encoding to save VMAF/PSNR budget. | [`learned_filter_v1`](models/learned_filter_v1.md) |
| **C4** | LLM dev helpers | repo-time only | Review, commit-message drafting, docgen. Never linked into libvmaf. | none (lives in [`dev-llm/`](../../dev-llm/)) |

C1, C2 and C3 share one runtime, the ONNX Runtime C API.

## How the pieces fit together

```figure
tiny-ai-pipeline
```

Training lives in Python and depends on PyTorch and Lightning. Runtime lives in
C and depends only on ONNX Runtime. The boundary is the `.onnx` plus sidecar
JSON pair on disk. Git LFS is not used, see
[tiny-blob-storage.md](tiny-blob-storage.md).

## Runtime availability

The tiny-AI extractors (`lpips`, `dists_sq`, `fastdvdnet_pre`, `mobilesal`,
`transnet_v2`) need a libvmaf build with ONNX Runtime support:

- On a build compiled with `-Denable_dnn=disabled`, extractor `init` returns
  `-ENOSYS` before probing `model_path` or the extractor-specific environment
  variable.
- On DNN-enabled builds, a missing model path stays a normal configuration
  error and returns `-EINVAL`.

## When to reach for Tiny AI

| You want to… | Use | Read |
| --- | --- | --- |
| Beat the upstream SVM's PLCC on your own MOS data | C1 | [training.md](training.md) |
| Score VMAF without a reference | C2 | [inference.md](inference.md) |
| Pre-filter frames before encoding | C3 | [inference.md](inference.md) |
| Compare a new model's PLCC/SROCC/RMSE to the SVM baseline | none | [benchmarks.md](benchmarks.md) |
| Understand the operator allowlist and signature model | none | [security.md](security.md) |

## Documentation rule

Every PR that adds or changes a tiny-AI surface ships its documentation in the
same PR. The project-wide rule is
[agent hard rules](../development/agent-hard-rules.md) (rule 7); the tiny-AI
five-point bar is [per-pr-doc-bar.md](per-pr-doc-bar.md)
([ADR-0042](../adr/0042-tinyai-docs-required-per-pr.md)).

## Related documents

- [roadmap.md](roadmap.md): status of each tiny-AI item and the Wave 1 scope
  (LPIPS, saliency, per-shot CRF, `vmaf_post`, allowlist `Loop`/`If`, MCP VLM
  tool).
- [training.md](training.md): `vmaf-train` CLI, dataset manifests, export flow.
- [inference.md](inference.md): CLI, C API and ffmpeg filter surfaces.
- [benchmarks.md](benchmarks.md): accuracy and throughput methodology.
- [security.md](security.md): operator allowlist, size cap, Sigstore
  verification.

## Per-model reference

Every shipped tiny-AI checkpoint has its own usage page under
[`models/`](models/), as
[ADR-0042](../adr/0042-tinyai-docs-required-per-pr.md) requires. Pages by
capability:

- Full-reference regressors:
  [vmaf_tiny_v1](models/vmaf_tiny_v1.md),
  [vmaf_tiny_v1_medium](models/vmaf_tiny_v1_medium.md) (legacy baselines),
  [vmaf_tiny_v2](models/vmaf_tiny_v2.md),
  [vmaf_tiny_v3](models/vmaf_tiny_v3.md),
  [vmaf_tiny_v4](models/vmaf_tiny_v4.md) (progressive VMAF-tiny series; the
  [v5 corpus-expansion proposal](../adr/0287-vmaf-tiny-v5-corpus-expansion.md)
  remains deferred and no `vmaf_tiny_v5.onnx` is shipped),
  [fr_regressor_v1](models/fr_regressor_v1.md),
  [fr_regressor_v2](models/fr_regressor_v2.md),
  [fr_regressor_v2 codec-aware](models/fr_regressor_v2_codec_aware.md),
  [fr_regressor_v2 probabilistic](models/fr_regressor_v2_probabilistic.md),
  [fr_regressor_v3](models/fr_regressor_v3.md).
- Perceptual distance:
  [LPIPS-SqueezeNet](models/lpips_sq.md) ([registry card](models/lpips_sq_v1.md)),
  [DISTS-Sq](models/dists_sq.md) (smoke checkpoint).
- No-reference and MOS heads:
  [nr_metric_v1](models/nr_metric_v1.md),
  [KoNViD MOS head v1](models/konvid_mos_head_v1.md).
- Filters and pre-filters:
  [learned_filter_v1](models/learned_filter_v1.md),
  [fastdvdnet_pre](models/fastdvdnet_pre.md) (5-frame temporal pre-filter).
- Saliency and shots:
  [saliency_student_v1](models/saliency_student_v1.md),
  [saliency_student_v2](models/saliency_student_v2.md),
  [mobilesal](models/mobilesal.md),
  [u2netp mirror](models/u2netp_mirror_card.md),
  [transnet_v2](models/transnet_v2.md) (shot-boundary detector, about 1M
  parameters).
- CI smoke fixtures, not quality models:
  [smoke_v0](models/smoke_v0.md),
  [smoke_v0_symbolic_batch](models/smoke_v0_symbolic_batch.md),
  [smoke_fp16_v0](models/smoke_fp16_v0.md),
  [smoke_multi_output_v0](models/smoke_multi_output_v0.md).
