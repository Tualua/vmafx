<!-- markdownlint-disable MD013 -->
# VMAFx documentation

VMAFx measures perceptual video quality: it scores how a distorted video
compares with its reference, the way a viewer would judge it. It is a fork of
[Netflix's VMAF](https://github.com/Netflix/vmaf) that keeps Netflix's reference
scores, adds CUDA, SYCL, HIP and Metal GPU backends and SIMD paths, and holds
every GPU result to the CPU's.

## Start here

1. [Get VMAFx](getting-started/index.md): a container image, a release
   download or a source build.
2. [Score your first pair](getting-started/first-score.md) and read the
   output.
3. [Choose a backend](backends/index.md) for your hardware.
4. Use it through the [CLI](usage/cli.md), the [C API](api/index.md) or the
   [FFmpeg filter](usage/ffmpeg.md).
5. Look things up in the reference below, or [contribute](https://github.com/VMAFx/vmafx/blob/master/CONTRIBUTING.md).

Found a problem? [Open an issue](https://github.com/VMAFx/vmafx/issues).
Have hardware the project does not own? Run the
[tester image](usage/tester-image.md) and send the report.

## Use VMAFx

| Topic | Pages |
| --- | --- |
| Install and build | [Getting started](getting-started/index.md), [Building on Windows](getting-started/building-on-windows.md), [Docker](usage/docker.md) |
| Command line | [CLI reference](usage/cli.md), [`--precision`](usage/precision.md), [environment variables](usage/env-vars.md), [`vmaf_bench`](usage/bench.md) |
| Integrations | [FFmpeg](usage/ffmpeg.md), [Python library](usage/python.md), [MATLAB](usage/matlab.md), [external resources](usage/external-resources.md) |
| Encoding workflows | [`vmaf-tune`](usage/vmaf-tune.md) ([fast path](usage/vmaf-tune-fast-path.md), [bitrate ladder](usage/vmaf-tune-ladder.md), [codec adapters](usage/vmaf-tune-codec-adapters.md), [recommend](usage/vmaf-tune-recommend.md), [saliency-aware](usage/vmaf-tune-saliency-aware.md), [resolution-aware](usage/vmaf-tune-resolution-aware.md), [HDR and sampling](usage/vmaf-tune-hdr-and-sampling.md), [cache](usage/vmaf-tune-cache.md), [bisect](usage/vmaf-tune-bisect.md)), [per-shot scoring](usage/vmaf-perShot.md), [ROI scoring](usage/vmaf-roi.md), [BD-rate utilities](usage/bd-rate.md) |
| Testing on your hardware | [tester image and macOS bundle](usage/tester-image.md), [build-from-source tester guide](usage/rc1-tester-guide.md), [hardware reports](hardware-reports/index.md) |

## Reference

| Topic | Pages |
| --- | --- |
| Metrics | [Feature reference](metrics/features.md): every extractor, its options and its GPU twins; [CAMBI](metrics/cambi.md), [SSIMULACRA 2](metrics/ssimulacra2.md), [DISTS](metrics/dists.md), [confidence interval](metrics/confidence-interval.md), [bad cases](metrics/bad-cases.md), [AOM CTC](metrics/ctc/aom.md) ([AOM](http://aomedia.org/)), [NFLX CTC](metrics/ctc/nflx.md) |
| Models | [Overview](models/overview.md), [VMAF v1](models/v1.md), [datasets](models/datasets.md) |
| Backends | [Backend guide](backends/index.md), [x86 SIMD](backends/x86/avx512.md), [ARM SIMD](backends/arm/overview.md), [CUDA](backends/cuda/overview.md), [SYCL / oneAPI](backends/sycl/overview.md), [HIP / ROCm](backends/hip/overview.md), [Metal](backends/metal/index.md) |
| C API | [API overview](api/index.md): contexts, pictures, models, features; [GPU](api/gpu.md), [DNN sessions](api/dnn.md), [embedded MCP](api/mcp.md) |
| MCP servers | [Overview](mcp/index.md), [tool reference](mcp/tools.md), [embedded server](mcp/embedded.md), [release channel](mcp/release-channel.md) |
| Tiny AI | [Tiny-AI docs](ai/index.md), [training](ai/training.md), [training data](ai/training-data.md), [MOS corpora](ai/mos-corpora.md), [inference](ai/inference.md), [LOSO evaluation](ai/loso-eval.md), [predictor](ai/predictor.md), [conformal VQA](ai/conformal-vqa.md), [ensemble training kit](ai/ensemble-training-kit.md), [ensemble v2 runbook](ai/ensemble-v2-real-corpus-retrain-runbook.md), [quantization](ai/quantization.md), [quant epsilon](ai/quant-eps.md), [model registry](ai/model-registry.md), [roadmap](ai/roadmap.md) |
| Other | [FAQ](reference/faq.md), [references](reference/references.md), [papers](https://github.com/VMAFx/vmafx/tree/master/docs/reference/papers), [presentations](https://github.com/VMAFx/vmafx/tree/master/docs/reference/presentations), [benchmarks](benchmarks.md) |

## Project

| Topic | Pages |
| --- | --- |
| Plan and releases | [Roadmap and release candidates](roadmap.md), [release process](development/release.md) |
| Contributing | [Engineering principles](principles.md), [build flags](development/build-flags.md), [IDE setup](development/ide-setup.md), [CI](development/ci.md), [CI runners](development/ci-runners.md), [fuzzing](development/fuzzing.md) |
| Correctness | [Cross-backend gate](development/cross-backend-gate.md), [upstream parity guard](development/upstream-parity.md), [Python process execution](development/python-process-execution.md) |
| Upstream | [Upstream watchers](development/upstream-watchers.md), [FFmpeg patch refresh](development/ffmpeg-patches-refresh.md) |
| Architecture | [Repository layout](architecture/index.md), [Python-harness workspace](architecture/workspace.md), [ADR log](adr/README.md) |
| Security | [Repository security](development/repository-security.md), [OpenSSF Scorecard](development/ossf-scorecard.md), [Scorecard and CodeQL audit, 2026-09-04](security/scorecard-alerts-2026-09-04.md) |
