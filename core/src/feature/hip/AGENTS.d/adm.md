---
paths:
  - core/src/feature/hip/integer_adm_hip.c
  - core/src/feature/hip/integer_adm_hip.h
  - core/src/feature/hip/integer_adm/adm_csf.hip
  - core/src/feature/hip/integer_adm/adm_dwt2.hip
invariant: Integer ADM maintains exact CPU parity, staging buffer rules, int64 vertical sums, and single reflection clamping.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# adm_hip = CPU bits (ADR-1423, `EXACT_TWINS`)

Scores = `integer_adm.c`'s bits (gfx1036: 21 pairs, 6192 values with
`debug=true`). Rebase-sensitive:

- Host arithmetic = CPU routines of `integer_adm_kernels.h`, never a copy:
  `adm_csf_factors()` (weights), `adm_csf_den_ctx_init()` /
  `i4_adm_csf_den_ctx_init()` (border + every denominator shift, passed to the
  kernel), `adm_cm_ctx_init()` / `i4_adm_cm_ctx_init()` on an empty
  `AdmBuffer` + `adm_cm_result()` / `i4_adm_cm_result()` /
  `adm_csf_den_result()` / `i4_adm_csf_den_result()` (scores). No
  `dwt_quant_step()`, `adm_csf_factors()`, `conclude_adm_*()` definition in
  `integer_adm_hip.c`.
- `adm_skip_scale0`: numerator 0, denominator `(float)1e-10`, BOTH added to
  the frame sums like `integer_adm_scale0()`.
- `adm_csf_den.hip`: one block per row + band (`grid = 1 x rows x 3`, 128
  threads), thread sums in shared memory, ONE fold per row by thread 0
  through `adm_csf_den_round_row_total()` (`adm_cm_accumulator.h`, also the
  CPU's fold, ADR-1416).
  Never fold per thread / wave / block-of-columns: accumulator differs, score
  differs on low-detail frames (4e-7; 1.5e-5 on the test's sparse frame) and
  at scale 0 above 2^20 region samples.
- No logarithm in that file: fp32 `log2f(area) - 20` is off by one for 81
  areas just above a power of two (962x13542: `adm_scale0` 0.860 vs 0.979).
- Per frame: `adm_hip_stage_luma()` (upload) FIRST, then
  `hipMemsetAsync(buf->tmp_res)`, then kernels. Clear queued ahead of the
  upload = lost in the first context needing larger planes than earlier
  contexts of the process (recycled device memory -> NaN numerator, run
  fails). `hipMemset` at allocation = no fix: async on the null stream for
  device memory (hipamd `ihipMemset()`, ROCm 7.2). Rule for every twin:
  "Frame order: upload, clear, kernels" above (ADR-1427).
- Guards: `test_hip_adm_exact` (device, nine contexts in one process, two
  frames each, `==`), `test_hip_adm_exact_contract.py` (device-free, ten
  planted regressions).

## Integer ADM staging buffer requirement (ADR-1154, ADR-1211)

HIP pictures arrive with host pointers (host-pic backend, ADR-0530);
a host pointer handed to a device kernel faults the GPU
(T-HIP-INTEGER-ADM-GPU-PAGE-FAULT-2026-09-05). `integer_adm_hip.c`
reads a device copy of the scale-0 luma plane per side (ADR-1211,
PR #1370), since ADR-1408 the context's shared frame
(`adm_hip_stage_luma()`); rows are packed, so the kernel stride is `w`. The
ADR-1154 deferral is over: do not re-add `should_fail` to the HIP ADM
tests for it. `.flags` is still `0`, so model-driven dispatch under
`--backend hip` keeps the CPU `adm`; the twin runs when named
(`--feature adm_hip`). It has no AIM pass: `adm3_score` / `aim_score`
stay out of `provided_features[]`
(T-GPU-ADM-AIM-DEVICE-PASS-MISSING-SYCL-HIP-2026-09-05). Float ADM
(`float_adm_hip.c`) has its own staging.

## Integer ADM tiny frames (T-GPU-ADM-TINY-FRAME-SHIFT-2026-09-18)

- `init_fex_hip()` calls `adm_frame_size_check()` first, before any device
  resource. Bound = CPU bound (17x17).
- Host shift rounding constant = `adm_half_shift(x)`. In-kernel scale-0 shift
  in `adm_cm_reduce_line_kernel_body` -> guarded ternary, 0 when shift = 0.
  Never bare `1u << (x - 1)`.
- Scale-0 CM kernel (`adm_cm_line_kernel_body`): `x + 1` -> `min(.., w - 1)`,
  `y + 1` -> `min(.., h - 1)`; `x - 1`, `y - 1` -> `abs()` (ADR-1210 rule).
- HIP twin emits no `adm3_score` (T-GPU-ADM-AIM-DEVICE-PASS-MISSING-SYCL-HIP-2026-09-05).
  Shared CUDA/HIP tests skip `adm3` under `HAVE_HIP`.
- HIP ADM tests run without `should_fail` since ADR-1211 staging; all pass on
  gfx1036. Do not re-add `should_fail` to hide a failure.

## Integer ADM 16-bit vertical DWT sums in int64 (T-GPU-ADM-DWT2-16BIT-INT32-OVERFLOW-2026-09-18)

- `core/src/feature/hip/integer_adm/adm_dwt2.hip`, scale-0 fused kernel: vertical accumulator = `DwtVertAccum<T>::type`
  -> int64 for `uint16_t`, int32 for `uint8_t`.
- Low-pass taps 1-3 sum 50582 -> int32 sum overflows (UB) once 3 16-bit
  samples >= 42456. CPU twin: `adm_dwt2_vpass16_tap4()` (int64).
- Normalised value fits int32 -> int64 form = old wrapped result. Scores
  identical; never narrow back to int32 for speed.
- Guard: `test_gpu_adm_bright_16bit_parity` in `test_gpu_adm_tiny_frames.c`
  (parity only; device wrap hides the UB itself).

## Tile loads and ADM scale-0 rows clamp after one reflection (ADR-1381)

- Tiled kernels load whole tile for every thread, padding threads included.
  One reflect-101 keeps every consumed sample in plane, not padding samples:
  17-sample motion plane reflects halo 33 to -1.
- Motion tile loads: `vmaf_hip_tile_index(vmaf_hip_reflect_101(i, n), n)`
  (`hip_tile_index.h`). Identity for every consumed sample -> no score change.
  `float_motion_score.hip` same geometry, same clamp (`fm_tile_index()`); its
  old `fm_mirror()` read before `ref_in` at extents 3-9 and 17.
- ADM scale-0 vertical DWT: `adm_dwt2_load_column()` reads
  `adm_dwt2_source_row()` (`integer_adm/adm_dwt2_rows.h`); launch geometry
  `ADM_DWT2_*` shared by kernel (`static_assert`) and `integer_adm_hip.c`.
  Bare reflection escapes only for heights 1-8; ADM minimum 17 -> identity.
  Scale 1-3 vertical kernels read per output row, in bounds from 2 rows.
- `test_hip_adm_dwt2_rows`: host replay of every launched thread row, heights
  1-8192, and every motion tile slot, extents 3-1024. Device-free, fast suite.
- New tiled HIP kernel: route every halo load through `vmaf_hip_tile_index()`.
