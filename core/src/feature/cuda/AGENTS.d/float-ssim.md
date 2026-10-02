---
paths:
  - core/src/feature/cuda/float_ssim_cuda.h
  - core/src/feature/cuda/ssim_cuda.c
invariant: float_ssim sums in CPU raster order on host and reproduces CPU pipeline per scale.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# float_ssim CPU raster order and pipeline

- **`float_ssim_cuda` frame sums = CPU raster order, on host** (ADR-1464,
  form of ADR-1424). `iqa_ssim()` = ONE double per sum (`ssim_sum`, `l_sum`,
  `c_sum`, `s_sum`), every window added left to right, top to bottom; double
  sum = its order. Pass-2 kernels REDUCE NOTHING: `calculate_ssim_vert_combine`
  stores `l * c * s` at `y * w_final + x`; `_lcs` stores 4 doubles per window
  (ssim, l, c, s; `LCS_TERMS` == `FLOAT_SSIM_LCS_SUMS`). Host:
  `float_ssim_frame_sum()` / `float_ssim_frame_sums_lcs()` add in index order,
  the ONLY place terms are added. NEVER bring back `__shfl` / `__shared__
  double` / block partials: per-block sum of same terms = neighbouring float
  on `core/test/float_ssim_order_frame.h` (CPU `0xb4e2b622`, block sum
  `0xb4e2b621`). Fixture header shared with HIP + SYCL tests, bytes fixed
  (sha256 in `test_cuda_float_ssim_exact_contract.py`): never edit. Tests:
  `test_cuda_float_ssim_order` (device), `test_cuda_float_ssim_exact_contract.py`
  (device-free). Cost: 8 B (32 B with `enable_lcs`) per window read back, about
  1 ns per window; tuning = `T-CUDA-FLOAT-SSIM-EXACT-THROUGHPUT-2026-10-02`.
  NEVER force identical windows to 1: CPU flat identical frames = 72.247 dB,
  not `+inf` (#1637 review). Both pass-2 kernels share `ssim_terms()`.
- **`float_ssim_cuda` = CPU pipeline on device, every scale** (ADR-1399).
  Mirror list, same PR when CPU side changes: `ssim.c` low-pass tap
  (`1.0f / (scale * scale)`) + `iqa/decimate.c::iqa_decimate` ->
  `ssim_score.cu::decimate_sample` (window `r - scale / 2`,
  `symmetric_index` = `KBND_SYMMETRIC`, exact int64 sum in units of 2^-52,
  ONE `__ll2float_rn`); `iqa/convolve.c` -> `add_tap` (fp32 product,
  `__dadd_rn` double sum, ONE `__double2float_rn` per pass, both passes);
  plane size = `iqa/decimate_dim.h::iqa_decimate_dim` (host
  `decimated_extent`). Every rounding there is an explicit `_rn` intrinsic; keep it so
  (fatbin has `--fmad=false`, ADR-1403, but the intrinsics are the contract). Gate: `check_context_cuda`
  and `init` share `float_ssim_geometry_supported` (plane >= 11x11, scale <=
  128). Scale 1 reads picture direct (`horiz_{8,16}bpc`), above reads
  `d_ref` / `d_cmp` (`horiz_planes`); one templated `horizontal_pass` body.
  Kernel argument arrays follow signatures exactly (ADR-1215). No host wait
  in submit. Result: score == CPU on every measured frame; frame sum order
  still per block. Guards: `test_cuda_float_ssim_decimate` (planes byte
  for byte), `test_cuda_float_ssim_parity` (+ `_large`, equality),
  `test_cuda_kernel_source_contract.py`,
  `test_gpu_float_ssim_auto_scale_contract.py`.
