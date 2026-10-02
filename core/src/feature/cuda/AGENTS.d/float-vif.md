---
paths:
  - core/src/feature/cuda/float_vif_cuda.c
  - core/src/feature/cuda/float_vif_cuda.h
invariant: float_vif options must be kernel arguments and match CPU reference bits.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# Kernel option and scoring invariants

- **`float_vif` options must be kernel ARGUMENTS, never kernel-local
  constants** (ADR-1217) — `vif_sigma_nsq` and `vif_enhn_gain_limit`
  = `VMAF_OPT_FLAG_FEATURE_PARAM` options on every float-VIF twin.
  Until ADR-1217 all three GPU compute kernels declared
  `const float vif_sigma_nsq = 2.0f; const float vif_egl = 100.0f;`
  locally. Non-default value accepted, folded into derived
  feature name, then discarded — including
  `vif_enhn_gain_limit = 1.0` that `model/vmaf_float_v0.6.1neg.json`
  sets on all four VIF scales, publishing non-NEG scores under NEG
  keys. Pass both plus host-derived `sigma_max_inv` in; missing
  kernel argument = compile error, shadowing local is not.
  `sigma_max_inv` derived on host exactly as
  `vif_tools.c::vif_statistic_s` derives it —
  `powf(nsq, 2.0f)` in `float`, divided by `255.0 * 255.0` in `double`,
  narrowed to `float` — so default path stays bit-identical; do not
  recompute it in device code. Twins in scope: `float_vif_cuda.c` (+
  `float_vif/float_vif_score.cu`, one launch site `fvif_launch_scale()`,
  `vif_sigma_nsq` passed as `double` since ADR-1412),
  `../sycl/float_vif_sycl.cpp`, `../hip/float_vif_hip.c` (+
  `../hip/float_vif/float_vif_score.hip`). Guarded by
  `test_float_vif_options_reach_kernel` variant in each backend's
  float-VIF parity test (CUDA: `test_model_options_exact`).
- **`float_vif_cuda` = CPU bits** (ADR-1412, `EXACT_TWINS`). Four things,
  each alone breaks identity:
  (1) taps from host `vif_get_filter()` (`float_vif_init_taps()`), passed
  by value in `FloatVifCudaTaps`; NO tap literal in any kernel file;
  (2) `fvif_log2()` = `vif_tools.c::log2f_approx()` (CPU never calls libm
  `log2f`: `VIF_OPT_FAST_LOG2`); no `log2f()` in kernel;
  (3) `fvif_pixel_statistic()` = `vif_pixel_statistic_s()`, `vif_sigma_nsq`
  fp64, log arguments fp64 rounded once, `MAX` / `MIN` as CPU ternaries
  (not `fmaxf` / `fminf`);
  (4) sums = `vif_statistic_s()` order: `float_vif_compute` stores two
  terms per pixel (column-major, `fvif_term_index()`),
  `float_vif_row_sums` one thread per row left to right, host
  `fvif_sum_rows()` top to bottom, fp32 both. NO warp / block / atomic
  reduction, no fp64 host sum.
  Arithmetic + argument blocks live in `../float_vif_gpu_common.h` (plain C,
  shared with `float_vif_hip`, ADR-1444); `float_vif/float_vif_device.h` =
  CUDA spelling only: maps `FVIF_F*` / `FVIF_D*` to explicit `__f*_rn` /
  `__d*_rn` under `DEVICE_CODE`, includes the common header, aliases
  `FloatVifCuda*` = `FloatVifGpu*`. Never drop the mapping: common header's
  default = plain operators, nvcc may contract them. Mirror list,
  same PR when CPU side changes: `vif_get_filter()`, `log2f_approx()` /
  `VIF_OPT_FAST_LOG2`, `vif_pixel_statistic_s()`, `vif_statistic_s()`,
  `vif_filter1d_*_s()` tap order, `picture_copy()`. Guards:
  `test_float_vif_device_math` (device-free, bit compare vs
  `vif_statistic_s()` + `compute_vif()`),
  `test_cuda_float_vif_exact_contract.py`, `test_cuda_float_vif_parity`
  (`==`, 7 cases). Option table carries CPU `vif_scale1..3_min_val`.
  Throughput debt: `T-CUDA-FLOAT-VIF-EXACT-THROUGHPUT-2026-10-01`; tune
  only with both tests green.
