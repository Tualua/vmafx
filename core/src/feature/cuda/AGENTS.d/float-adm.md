---
paths:
  - core/src/feature/cuda/float_adm_cuda.c
  - core/src/feature/cuda/float_adm_cuda.h
invariant: float_adm AIM/ADM3 scores, exact CPU bits, and kernel argument delivery.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# float_adm AIM/ADM3, exact bits, and kernel options

- **`float_adm_cuda.c` / `float_adm/float_adm_score.cu` AIM/ADM3
  slot layout** (ADR-0574, relaid by ADR-1420). `FADM_TERM_SLOTS = 9`
  lives in ONE place, `../float_adm_gpu_common.h` (shared with
  `float_adm_hip` since ADR-1458; `float_adm/float_adm_device.h` = CUDA
  spelling only: `__fmul_rn()` family, includes it, typedefs the
  `FloatAdmCuda*` names), included by host
  and kernels: `FADM_SLOT_DEN` 0..2, `FADM_SLOT_CM` 3..5,
  `FADM_SLOT_AIM` 6..8 (h, v, d each). Term buffer =
  `fadm_term_index()` (slot, then column, then row); row-sum buffer =
  `fadm_row_index()`, per-scale span at `row_offset[scale]`. Never
  index either by hand. Pre-ADR-1420 `FADM_ACCUM_SLOTS`, per-WG
  accumulators, `float_adm_csf_cm` / `float_adm_csf_r` /
  `float_adm_aim_cm` = gone; a rebase that brings them back brings
  back the 1.3e-5 twin. `--fmad=false` (every fatbin, ADR-1403)
  covers all five kernels; do not remove it. AIM/ADM3 options:
  `adm_bypass_cm`, `adm_adm3_apply_hm`, `adm_p_norm`,
  `adm_dlm_weight`, `adm_min_val`, `adm_skip_aim_scale` must keep
  same defaults as `float_adm.c`.

- **`float_adm_cuda` = CPU bits** (ADR-1420, `EXACT_TWINS`). Each alone
  breaks identity:
  (1) `fadm_divs()` = `adm_tools.c::DIVS()` = IEEE fp32 quotient since
  ADR-1442 (CPU divides on every host). Device: `FADM_FDIV` =
  `__fdiv_rn()`, never plain `/` (flag-proof), never a reciprocal, no
  host probe, no table (`adm_reciprocal_model.*` deleted). No
  `--use_fast_math` / `-prec-div=false` in CUDA flags.
  `test_float_adm_divides_contract.py` guards;
  (2) angle threshold `(cos^2 * |o|^2) * |t|^2`, that association
  (1.3e-5 alone); `cos^2` from `adm_decouple_cos_1deg_sq_s()`;
  (3) gain limit fp64 kernel argument, product fp64, rounded once;
  clamps = CPU ternaries, two sequential `if`, not `else if`, not
  `fminf` / `fmaxf`;
  (4) `FADM_ONE_BY_30` / `FADM_ONE_BY_15` double literals: 1/30 product
  fp64, centre tap an fp64 addend; no `f` suffix;
  (5) threshold = one nine-term sum per band, centre FIFTH, then the
  three band sums (`fadm_thresh_band()` / `fadm_threshold()`);
  (6) sums = CPU order: `float_adm_terms` stores nine terms per sample
  of reduced region, `float_adm_row_sums` one thread per (slot, row)
  left to right, host `fadm_fold_rows()` top to bottom, fp32 both. NO
  warp / block / atomic reduction, no fp64 host sum of partials;
  (7) host concludes with CPU routines (`adm_float_reference.h`):
  `adm_csf_rfactor_s()` (no copy of `dwt_quant_step()`: old copy was
  1-3 ulp off on 4 of 8 default weights), `adm_border_s()`,
  `adm_pool_bands_s()`; floor of frame sums `1e-10`, not `1e-2`.
  Not exact, by design: `adm_p_norm` other than 1 or 3 (device `powf`
  vs glibc, 1.1e-7). Frames < 17x17: CPU and twin both refuse
  (`adm_frame_size_check()`). Mirror list, same
  PR when CPU side changes: `adm_decouple_s()`, `adm_csf_s()`,
  `adm_cm_thresh3x3_s()`, `adm_csf_den_scale_s()`, `adm_cm_s()`,
  `DIVS()`, `ADM_OPT_AVOID_ATAN`, `compute_adm()` floor. Guards:
  `test_float_adm_device_math` (device-free, bit compare vs those
  routines + decouple vs IEEE quotient),
  `test_cuda_float_adm_exact_contract.py`,
  `test_cuda_float_adm_parity` (`==`, 15 cases + `apn` tolerance case).
  Throughput debt: `T-CUDA-FLOAT-ADM-EXACT-THROUGHPUT-2026-10-01`; tune
  only with both tests green.
- **`float_adm` options must reach KERNELS, not option
  table alone** (ADR-1220) — `adm_p_norm` (`apn`), `adm_bypass_cm` (`bcm`)
  and, on Metal, `adm_skip_scale0` (`ssz`) =
  `VMAF_OPT_FLAG_FEATURE_PARAM` options twins declare with
  CPU's names, aliases, defaults and ranges. Until ADR-1220,
  kernels hardcoded cube sum; host pooling hardcoded
  `1.0f / 3.0f` root. So `apn` moved only AIM exponent, produced
  hybrid quantity; `bcm` was read by nothing at all (`grep`
  returned only struct field and option entry). Three rules:
  (1) `adm_p_norm` has FOUR application points — DLM numerator sum,
  CSF denominator sum, pooling root, and
  `get_noise_constant()` — change them together; (2) keep CPU's
  `p == 3` literal-cube fast path in kernel, because device
  `powf(x, 3.0f)` not guaranteed to equal `x * x * x`, and
  default path must not move; (3) `adm_bypass_cm` gates BOTH DLM
  and AIM `adm_cm()` call. Guarded by
  `test_float_adm_*_reaches_kernel` variants in each backend's
  float-ADM parity test, which read ADR-1183-derived
  `adm2_apn_2` / `adm2_bcm_1` keys — default-options test cannot
  see any of this, because `p = 3` IS hardcoded exponent.
