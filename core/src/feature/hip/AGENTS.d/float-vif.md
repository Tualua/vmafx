---
paths:
  - core/src/feature/hip/float_vif_hip.c
  - core/src/feature/hip/float_vif_hip.h
  - core/src/feature/hip/float_vif/float_vif_score.hip
invariant: float_vif options must be kernel arguments and match CPU reference bits.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# float_vif options must be kernel arguments (ADR-1217)

`vif_sigma_nsq` and `vif_enhn_gain_limit` = `VMAF_OPT_FLAG_FEATURE_PARAM`
options `float_vif_hip` declares in its option table. Until
ADR-1217, compute kernel in `float_vif/float_vif_score.hip` declared
them as kernel-local constants at their default values. Non-default
value was accepted, range-checked, folded into derived feature name
(ADR-1183), then discarded. That included
`vif_enhn_gain_limit = 1.0` that `model/vmaf_float_v0.6.1neg.json` sets on all four VIF
scales, so HIP run of NEG model published ordinary
enhancement-gain-enabled scores under NEG feature keys.

Invariant: `float_vif_compute` takes `vif_sigma_nsq`, `vif_egl` and
`sigma_max_inv` as trailing kernel arguments; `fvif_launch_compute()`
derives all three from `FloatVifStateHip`. `sigma_max_inv` computed
host-side exactly as CPU computes it in
`vif_tools.c::vif_statistic_s` — `powf(nsq, 2.0f)` in `float`,
divided by `255.0 * 255.0` in `double`, narrowed to `float` — so
default path stays bit-identical to pre-ADR-1217 constant. Do not
recompute it in device code: device `powf` not guaranteed to round
like host's, and value feeds `sigma1_sq < vif_sigma_nsq` branch that
writes `num_val` directly.

`hipModuleLaunchKernel` silently ignores surplus `kernelParams`
entries, reads uninitialised memory for missing ones, so signature
and `args[]` array must change together. Guard =
`test_hip_float_vif_parity.c::test_float_vif_options_reach_kernel`,
which pins `egl=1.0 snsq=1.5`, asserts parity on derived
`vif_scale0_egl_1_snsq_1.5` key — default-options test cannot see
this class of defect, because hardcoded values *were* defaults.

## float_vif_hip = CPU bits (ADR-1444)

- Exact twin `float_vif` (`scripts/ci/exact_twins.d/float_vif.hip`); gfx1036:
  712 of 712 scores, 1922 values with debug + options.
- Arithmetic + argument blocks = `../float_vif_gpu_common.h`, shared with
  `float_vif_cuda` (ADR-1412). `float_vif/float_vif_score.hip` = tiling only:
  decimate, per-pixel terms (column-major, `fvif_term_index()`), one thread
  per row (`fvif_row_sum()`). Host `fvif_sum_rows()` top to bottom, fp32.
- Never in the `.hip` file: a tap literal, `log2f()`, `__shfl_*` / `warpSize`
  / atomic reduction, a `#define FVIF_F*` / `FVIF_D*`. Defaults = plain
  operators; `hip_strict_fp_args` makes them round once. fp64 `+` `/` on
  gfx1036 = host bits (33.5 M operand pairs). Another device: re-measure
  before declaring.
- Host: taps = `vif_get_filter()` (`fvif_hip_init_taps()`), `.taps` +
  `.vif_sigma_nsq` (double) in one by-value block per launch.
- Tile loads go through `vmaf_hip_tile_index()`. Old kernel reflected once,
  no clamp: frame < 72 px either way = GPU memory fault (64x64, 56x56, 40x40,
  3 of 3 runs each).
- Options = CPU table incl. `vif_scale1..3_min_val`.
- Cost: 20.7 -> 26.0 ms 1080p, 86.0 -> 147.1 ms 4K. 4K: term plane 66 MB =
  37.8 ms, fp64 = 8.3 ms. `T-HIP-FLOAT-VIF-EXACT-THROUGHPUT-2026-10-02`.
- Guards: `test_hip_float_vif_parity` (+ `_large`),
  `test_hip_float_vif_exact_contract.py`, `test_float_vif_device_math`.
