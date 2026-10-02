---
paths:
  - core/src/feature/cuda/integer_ms_ssim_cuda.c
  - core/src/feature/cuda/integer_ms_ssim_cuda.h
invariant: MS-SSIM exact CPU arithmetic, option flags, and clip_db ceiling semantics.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# MS-SSIM CPU arithmetic, options, and clip_db

- **`float_ms_ssim_cuda` = CPU arithmetic, bit for bit (ADR-1403).** On
  RTX 4090: 104 / 104 frames identical (Netflix pair, 1080p checkerboards,
  BBB 4K), `enable_lcs` atoms included. Keep all five:
  (1) decimate taps = `__fmaf_rn(sample, tap, acc)` (`ms_ssim_decimate.c`
  uses `vmaf_fmaf_exact()`); (2) window sums = fp32 products into `MsPair`
  (exact fp32 two-sum pair standing for `iqa_convolve()`'s fp64 sum; plain
  fp64 gives same scores, +3.4 ms per 4K frame); (3) `l` / `c` / `s` =
  `ssim_accumulate_default_scalar()` types: fp64 numerators for `l`, `c`,
  fp32 denominators, fp32 quotient for `s`, root = `__fsqrt_rn()` (clang
  CUDA turns `sqrtf()` into `sqrt.approx`); (4) host constants fp32
  (`C1`, `C2`, `C3 = C2 / 2.0f`); (5) host rounds each per-scale mean to
  fp32 before `pow()`, with `fabs()` on all three as `ms_ssim.c`. Guards:
  `test_cuda_kernel_source_contract.py` (six planted regressions),
  `test_cuda_float_ms_ssim_parity` (`==` on 16 outputs x 3 frames; fails on
  pre-ADR-1403 code).
  **Sums = CPU raster order, on host (ADR-1465).** `iqa_ssim()` = ONE double
  per sum (`l_sum`, `c_sum`, `s_sum`), per scale, every window left to right,
  top to bottom. `ms_ssim_vert_lcs` REDUCES NOTHING: stores `l`, `c`
  (double) and `s` (float, it is one) at `y * w_final + x` of three planes
  per scale; host `ms_ssim_scale_sums()` adds in index order, the ONLY place
  terms are added. NEVER bring back `__shfl` / `__shared__ double` / block
  partials: per-block sums = neighbouring float on
  `core/test/float_ms_ssim_order_frame.h` (shared with HIP + SYCL tests:
  `float_ms_ssim_c_scale1` CPU `0x3f7c49a0`, block sum `0x3f7c499f`) and on
  the formula frame in `test_cuda_float_ms_ssim_order.c`
  (`float_ms_ssim_l_scale0` CPU `0x3f7cd999`, block sum `0x3f7cd998`); 4 such
  frames in 8.32e6 noise frames on CUDA.
  Tests: `test_cuda_float_ms_ssim_order` (device),
  `test_cuda_float_ms_ssim_exact_contract.py` (device-free). Cost: 20 B per
  window read back, about 2.1 ns per window, 2.5x to 3.3x the frame time;
  tuning = `T-CUDA-FLOAT-MS-SSIM-EXACT-THROUGHPUT-2026-10-02`. SYCL / HIP / Metal twins still
  old arithmetic: `T-GPU-FLOAT-MS-SSIM-CPU-ARITHMETIC-2026-10-01`.
- **`integer_ms_ssim_cuda.c` honours `enable_lcs`, `enable_db`,
  `clip_db` GPU contracts** (ADR-0243, ADR-0460). Emits 15 extra
  metrics (`float_ms_ssim_{l,c,s}_scale{0..4}`) when `enable_lcs=true`,
  all `l_scale*` first then `c_*` then `s_*` (metric ordering =
  public API; renaming or reordering breaks cross-backend parity
  gate). Returns dB-domain score (`-10*log10(1-ms_ssim)`) when
  `enable_db=true`, optionally clipping via `clip_db`. See
  [../../AGENTS.md §"MS-SSIM `enable_lcs` GPU
  contract"](../../../AGENTS.md).

- **MS-SSIM `clip_db` = CEILING on dB output, not clamp on
  linear score** (ADR-1221) — `float_ms_ssim.c` derives
  `max_db = ceil(10 * log10(peak * peak / mse))` with
  `mse = 0.5 / (w * h)` at `init()`; `convert_to_db()` returns
  `MIN(-10*log10(1 - score), max_db)`, short-circuiting to `max_db`
  when `score >= 1.0`. Until ADR-1221 all three twins clamped
  LINEAR score into `[0, 1]`, converted with no ceiling; none
  had `max_db` field: identical reference/distorted pair returned
  `+Inf`, every high-similarity pair returned uncapped dB value.
  Keep `max_db` and `ms_ssim_convert_to_db()` in sync with CPU
  across `integer_ms_ssim_cuda.c`, `../sycl/integer_ms_ssim_sycl.cpp`
  and `../hip/integer_ms_ssim_hip.c`. Guard =
  `test_*_ms_ssim_*clip_db_ceiling` variant, which must feed
  IDENTICAL pair — on merely high-similarity fixture ceiling
  never binds, variant passes against unfixed twin.
