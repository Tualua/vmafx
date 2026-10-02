---
paths:
  - core/src/feature/feature_collector.cpp
  - core/src/feature/feature_name.cpp
invariant: Model options gate GPU twin selection; every option must be parsed or safely rejected.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# Model Options Gate GPU Twin Selection

- **Model options gate GPU twin selection (ADR-1183)**: every option
  model sets in its feature options dictionary must be present in
  chosen GPU twin's option table. If GPU twin lacks any requested option
  (e.g. `integer_adm_cuda` lacking `adm_csf_mode`),
  `vmaf_use_features_from_model` in `core/src/libvmaf.c` rejects GPU twin
  and dispatches extractor to CPU reference. Option parsing in
  `feature_extractor.cpp` rejects any unknown dictionary keys with
  `-EINVAL`. On rebase, do not bypass this validation or revert to silent
  option omission.
  **ADR-1316 extends that gate to option values:** a twin that mirrors the CPU
  table for collector-key parity, whose CPU twin can execute the full range,
  but implements only the default marks that entry
  `VMAF_OPT_FLAG_DEFAULT_ONLY`. Model-driven dispatch then chooses the CPU
  twin for a valid non-default value before device initialization. Keep the
  CPU name, alias, declared range and `FEATURE_PARAM` bit; never narrow the
  table to hide a backend capability gap. Extend
  `test_gpu_option_value_capability_contract.py` when adding or removing such
  a restriction.
  **ADR-1324 adds the dimension-dependent counterpart for GPU `float_ssim`:**
  all four twins keep the CPU-authored `scale=0` auto option, declare a
  `context_check` plus `float_ssim` CPU fallback, and use their existing scale
  helper as the sole threshold authority. Model-selected host-picture
  contexts whose resolved scale exceeds `1` are replaced after validation and
  backend preparation but before extractor initialization/submission and CUDA
  picture translation. SYCL shared staging may already be populated at that
  point. Directly named GPU extractors and
  the device-buffer-only SYCL entry point keep their scale-1-only errors. Do
  not mark `scale` default-only, narrow its range, or retry arbitrary init
  failures on CPU. Extend
  `test_gpu_float_ssim_auto_scale_contract.py` whenever this capability
  changes. **Since ADR-1370 `float_ssim_sycl` decimates on the device**
  (bit-identical to `iqa_decimate()`); its check refuses only a decimated
  plane under 11x11 or a scale above 128 via
  `float_ssim_geometry_supported()`. CUDA, HIP and Metal keep the scale-1
  rule until they port the same kernel.
