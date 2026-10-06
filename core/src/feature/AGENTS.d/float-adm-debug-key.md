---
paths:
  - core/src/feature/float_adm.c
  - core/src/feature/cuda/float_adm_cuda.c
  - core/src/feature/sycl/float_adm_sycl.cpp
  - core/src/feature/hip/float_adm_hip.c
  - core/src/feature/metal/float_adm_metal.mm
  - core/src/fex_ctx_vector.cpp
  - core/test/float_adm_twin_parity.h
  - core/test/test_float_adm_debug_key_refusal.c
invariant: float_adm's debug ratio `adm` is never suffixed, on the CPU and every twin; a second debug instance is refused.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# `float_adm` debug key (ADR-2056)

`float_adm` with `debug=true` files its ratio under the unsuffixed key `adm`; the Netflix
golden tests read it under every option set, so it is a contract. The CPU extractor and the
CUDA, SYCL, HIP and Metal twins list `adm_scale0` in `provided_features` (not `adm`, which
would make the feature-name dictionary suffix it) and declare `.unsuffixed_debug_key = "adm"`.
`feature_extractor_vector_append()` refuses a second context with the same declared key and
`debug` set. A sync must not put `adm` back into a twin's `provided_features`, and must not
rename the key. `test_float_adm_debug_key_refusal` and the option cases of
`test_cuda_float_adm_parity` / `float_adm_twin_parity.h` (the unsuffixed `adm` closes every
key list) guard it.
