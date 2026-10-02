---
paths:
  - core/src/feature/cuda/integer_cambi_cuda.c
  - core/src/feature/cuda/integer_adm_cuda.c
invariant: Host-side preprocessing in submit callbacks must download GPU to host first.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# Host-side preprocessing GPU download requirement

- **Host-side preprocessing in CUDA feature extractor `submit` callbacks
  must download GPU→host first.** Pictures passed to CUDA extractor's
  `submit()` have device pointers in `data[]`; host cannot read them
  directly. Use `vmaf_cuda_picture_download_async` followed by
  `cuStreamSynchronize` on picture's private stream (obtained via
  `vmaf_cuda_picture_get_stream`) before passing picture to any
  host-side function dereferencing `data[]`. CAMBI extractor used this
  pattern (Issue lusoris/vmaf#857, fixed by lusoris/vmaf#870) until
  ADR-1379 moved its preprocessing on device.
  All other CUDA extractors here
  currently keep preprocessing on GPU, not affected,
  but rule applies to any future extractor mixing GPU input
  pictures with host-side preprocessing.
