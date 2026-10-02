---
paths:
  - core/src/feature/cuda/integer_adm_cuda.c
  - core/src/meson.build
invariant: Scope, build, and governing ADR references for CUDA feature extractors.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# Scope

```text
feature/cuda/
  <feature>_cuda.{c,h}        # host glue: registration, submit/collect, kernel-template wiring
  <feature>/                  # subdirectory of `.cu` device code (where the host glue is non-trivial)
    *.cu                      # CUDA kernel TUs (compiled with nvcc)
    *.cuh                     # device-side helpers (included from .cu only)
```

Examples: `integer_psnr_cuda.c` = single-file consumer using
kernel-template flat shape; `integer_adm/` = multi-`.cu` consumer
because ADM splits across DWT2 + decouple + CSF + CM passes.

## Build

CUDA feature TUs compile only when `meson setup -Denable_cuda=true`.
`enable_cuda` umbrella flag gates inclusion via
`#if HAVE_CUDA` blocks in `feature/feature_extractor.c`.

## Governing ADRs

- [ADR-0182](../../../../../docs/adr/0182-gpu-long-tail-batch-1.md) +
  [ADR-0188](../../../../../docs/adr/0188-gpu-long-tail-batch-2.md) +
  [ADR-0192](../../../../../docs/adr/0192-gpu-long-tail-batch-3.md) —
  GPU long-tail batches. Every CUDA feature kernel here = row
  in one of these.
- [ADR-0214](../../../../../docs/adr/0214-gpu-parity-ci-gate.md) —
  GPU-parity CI gate.
- [ADR-0219](../../../../../docs/adr/0219-motion3-gpu-contract.md) —
  motion3 GPU contract.
- [ADR-0241](../../../../../docs/adr/0241-hip-first-consumer-psnr.md) —
  kernel-template mirror between CUDA and HIP.
- [ADR-0243](../../../../../docs/adr/0243-enable-lcs-gpu.md) — MS-SSIM
  `enable_lcs` GPU contract.
- [ADR-0246](../../../../../docs/adr/0246-cuda-kernel-template-feature.md) —
  per-feature CUDA kernel-template scaffolding.
- [ADR-0360](../../../../../docs/adr/0360-cambi-cuda.md) —
  CAMBI CUDA port (Strategy II hybrid, T3-15a).
- [ADR-0464](../../../../../docs/adr/0464-cambi-cuda-smem-tile.md) --
  CAMBI CUDA spatial-mask SLM tile (perf-audit 2026-05-16 win 3).
- [ADR-1379](../../../../../docs/adr/1379-cuda-cambi-device-resident-pipeline.md) —
  CAMBI device-resident (ADR-1357 design on CUDA).
- [ADR-1380](../../../../../docs/adr/1380-cuda-speed-device-resident-pipeline.md) —
  SpEED device-resident (ADR-1358 chain on CUDA).
- [ADR-0456](../../../../../docs/adr/0456-ssimulacra2-cuda-blur-fusion-transpose.md) —
  SSIMULACRA2 CUDA blur: 3-channel kernel fusion + V-pass transpose.
- [ADR-0574](../../../../../docs/adr/0574-hdr-features-cuda-twins-phase-1.md) —
  CUDA twins for HDR-model `aim` and `adm3` sub-features (Phase 1);
  slot layout now `FADM_TERM_SLOTS` in `float_adm_device.h` (ADR-1420).
