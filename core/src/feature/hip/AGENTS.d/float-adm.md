---
paths:
  - core/src/feature/hip/float_adm_hip.c
  - core/src/feature/hip/float_adm_hip.h
  - core/src/feature/hip/float_adm/float_adm_score.hip
invariant: float_adm options must reach kernels and compute bit-exact CPU results.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# float_adm options must reach the kernels (ADR-1220)

`adm_p_norm` (alias `apn`) = `VMAF_OPT_FLAG_FEATURE_PARAM` that
`float_adm_hip` declares with CPU's name, alias, default and range.
Until ADR-1220, `float_adm/float_adm_score.hip` hardcoded cube sum
and `float_adm_hip.c` hardcoded `1.0f / 3.0f` pooling root, so option
moved only AIM exponent, produced hybrid quantity.

Invariants:

- `adm_p_norm` has **four** application points in `adm_tools.c` —
  DLM numerator sum, CSF denominator sum, pooling root
  `powf(accum, 1.0f / adm_p_norm)`, and
  `get_noise_constant(w, h, weight, p)`. Change them together.
- Keep CPU's `p == 3` literal-cube fast path in kernel
  (`fadm_pnorm_term`). Device `powf(x, 3.0f)` not guaranteed to
  equal `x * x * x`; default path is what every shipped model uses.
- `hipModuleLaunchKernel` silently ignores surplus `kernelParams`,
  reads uninitialised memory for missing ones, so kernel signature
  and `args[]` arrays for **both** `func_csf_cm` and `func_aim_cm`
  change together.

`adm_bypass_cm` (`bcm`) declared and passed via `FadmScaleGeom.bypass_cm`
and kernel `args[]` into `float_adm_csf_cm` and `float_adm_aim_cm`,
bypassing 3x3 contrast-masking threshold when non-zero (CPU/CUDA/Metal
parity, ADR-1220). Guarded by
`test_hip_float_adm_parity.c::test_float_adm_bypass_cm_reaches_kernel`.

## float_adm_hip = CPU bits (ADR-1458)

- Exact twin: `scripts/ci/exact_twins.d/float_adm.hip`. gfx1036: 1246 of
  1246 values, 3204 of 3204 with `debug=true`, egl 1.2 / bcm / sasc / nvd /
  apn 1 identical.
- Arithmetic = `../float_adm_gpu_common.h`, shared with `float_adm_cuda`
  (ADR-1420): `adm_tools.c` operation for operation. Change there = both
  twins + `test_float_adm_device_math`.
- HIP spelling = `float_adm/float_adm_hip_math.h`: shared header's PLAIN
  operators (strict FP list, ADR-1407: no contraction, `/` correctly
  rounded = `DIVS()` since ADR-1442; fp64 `*` `+` IEEE on device). Never
  `__fmul_rn` / `__fdiv_rn` here. `FADM_POWF` = identity at exponent 1
  (device `powf(x, 1)` != x, 5.8e-13 on `aim`); other exponents except 3 =
  device `powf`, close not equal (apn 2: 1.5e-7).
- Kernels `float_adm/float_adm_score.hip` = the CUDA kernels: 5 launches per
  scale (dwt vert, dwt hori, decouple + both CSFs, 9 terms per sample, one
  fp32 sum per row and slot). No wave / block reduction, no constants of
  their own. Arg blocks by value (`FloatAdmGpu*Args`).
- Host: `adm_csf_rfactor_s()`, `adm_border_s()`,
  `adm_decouple_cos_1deg_sq_s()`, `adm_pool_bands_s()`
  (`../adm_float_reference.h`), rows added by `fadm_fold_rows()` in fp32,
  floor 1e-10, gain limit `double` to the kernel,
  `adm_frame_size_check()` FIRST in init (17x17 minimum, before any device
  resource). Option `adm_skip_aim_scale` present; `adm_skip_scale0` and
  `adm_f1sN` / `adm_f2sN` not (CUDA twin's option set).
- Cost none: 1080p 13.4 -> 13.2 ms, 4K 80.8 -> 68.4 ms (20 launches, was
  24).
- Guards: `test_hip_float_adm_parity` (+ `_large`, `==`),
  `test_hip_float_adm_math` (device vs host per value, 3 M samples; a
  reciprocal multiply fails at sample 16),
  `test_hip_float_adm_exact_contract.py`, `test_float_adm_device_math`,
  `test_float_adm_divides_contract.py`.
