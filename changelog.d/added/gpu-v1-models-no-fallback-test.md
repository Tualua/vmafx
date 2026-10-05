- **A device test holds every `vmaf_v1.0.16*` model wholly on CUDA, SYCL and
  HIP, bit for bit.** `test_cuda_v1_models_no_fallback`,
  `test_sycl_v1_models_no_fallback` and `test_hip_v1_models_no_fallback` score
  the eight built-in v1 models and the default model on the Netflix 576x324
  pair (8, 10 and 12 bits 4:2:0, 10 bits 4:2:2) and 16 frames of the BBB
  3840x2160 pair. Each test fails on any CPU extractor in `feature_backends`
  and on any value that differs from the CPU run at `--precision max`. See
  [the gate page](docs/development/cross-backend-gate.md#whole-models-on-one-backend).
