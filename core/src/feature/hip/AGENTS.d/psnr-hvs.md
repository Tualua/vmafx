---
paths:
  - core/src/feature/hip/integer_psnr_hvs_hip.c
  - core/src/feature/hip/integer_psnr_hvs_hip.h
  - core/src/feature/hip/integer_psnr_hvs/psnr_hvs_score.hip
invariant: HIP PSNR-HVS matches CPU reference scores bit for bit.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# `psnr_hvs_hip` = CPU scores bit for bit (ADR-1397, ADR-1401)

Kernel (`integer_psnr_hvs/psnr_hvs_score.hip`) stores the 64 terms
`calc_psnrhvs()` sums per block (`hvs_store_terms`, row-major, blocks in plane
then raster order); `psnr_hvs_plane_scores()` hands each plane to
`vmaf_psnr_hvs_plane_score()` (`../psnr_hvs_score.c`: one running `float`, CPU
order), combined score + dB via the same file. Same contract as the CUDA twin.
Load-bearing, each one breaks bit-identity on its own:

- masking table = `(csf * 0.3885746225901003)^2` in `double`, stored `float`
  (`hvs_mask_value`, constexpr -> static data);
- threshold = upstream's statement (ADR-1488): `product = energy * ratio` in
  `float` (plain operator, one rounding under the strict FP list), then
  `(float)(sqrt((double)product) / 32.0)` (`hvs_threshold`); each work-item
  forms its own, the reference item takes the larger after the barrier. Not
  `(double)energy * (double)ratio` (the fork's CPU between PR #552 and
  ADR-1488);
- `-ffp-contract=off` + `-fhip-fp32-correctly-rounded-divide-sqrt` on the
  HSACO (`hip_cu_extra_flags`);
- coefficient error = integer `abs()` cast to `float`;
- no sum of terms in kernel or host TU (per-block partials round differently:
  1e-2 dB at 3840x2160).

Plane count = `psnr_hvs_plane_count(s)` (`s->n_planes` clamped to 3): 1 for
`enable_chroma=false` or 4:0:0 (CPU `psnr_hvs.c::init` rule), else 3. Staging,
uploads, kernel `args.n_planes`, scores and emitted features all loop to it; a
fixed `PSNR_HVS_NUM_PLANES` loop there re-breaks 4:0:0 (reads `data[1]` of a
luma-only picture).

Guards: `test_hip_psnr_hvs_parity{,_large}` (device, `==` on all four outputs,
3840x2160, 4:0:0 and `enable_chroma=false` included), `test_psnr_hvs_twin_exact_sum_contract.py` and
`test_psnr_hvs_score` (device-free). Gate cell = tolerance 0 (`EXACT_TWINS`,
`scripts/ci/cross_backend_calibration.py`). Upstream change to
`calc_psnrhvs()` arithmetic or order -> mirror it here in the same PR.
Readback = 256 bytes per block (65 MB per 3840x2160 4:2:0 frame, device +
pinned); tuning tracked as T-SYCL-HIP-PSNR-HVS-EXACT-SUM-THROUGHPUT-2026-10-01,
must stay bit-exact.
