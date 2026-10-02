---
paths:
  - core/src/feature/hip/ssimulacra2_hip.c
  - core/src/feature/hip/ssimulacra2_hip.h
  - core/src/feature/hip/ssimulacra2/ssimulacra2_device.hip
invariant: ssimulacra2_hip produces bit-exact CPU results.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# ssimulacra2_hip = CPU bits (ADR-1445)

- Exact twin `ssimulacra2` (`scripts/ci/exact_twins.d/ssimulacra2.hip`);
  gfx1036: 178 of 178 frames, every `yuv_matrix`.
- Terms = CPU fp64 expressions, `ss2h_terms()` in
  `ssimulacra2/ssimulacra2_device.hip` (mirror of CUDA `ss2c_terms()`). No
  fp32 pairs (`two_sum` / `ff_add` / `ff_div` gone). gfx1036 fp64 `+ - * /`
  on these terms = host bits (16.8 M pixels probed). Another device:
  re-measure.
- Sums = `../ordered_sum.h`, four kernels as CUDA (ADR-1433):
  `ssimulacra2_chunk_sums` (tree, advice only), `_chunk_plan`,
  `_chunk_units`, `_ordered_totals`. Chunk = 256 lanes x 4 consecutive pixels,
  raster order. Lanes composed adjacent, lower first (`vmaf_ordsum_then` not
  commutative). Fallback adds chunk terms in pixel order.
- Only `ssimulacra2_ordered_totals` writes `totals`. Tree sums feed the plan,
  never the score.
- Terms must stay >= 0 or NaN.
- Readback 864 bytes = 108 doubles; `collect()` reads them as they are.
- Cost 1080p 58.1 -> 167.0 ms, 4K 233.7 -> 662.4 ms. 1080p split: chunk_sums
  36.9, chunk_plan 1.9, chunk_units 74.1, ordered_totals 8.5 ms (old tree
  ~12.5). Chunk 64 x 16 = slower (227 ms).
  `T-HIP-SSIMULACRA2-EXACT-THROUGHPUT-2026-10-02`.
- Guards: `test_hip_ssimulacra2_parity` (+ `_large`, `==`),
  `test_hip_ssimulacra2_exact_contract.py`, `test_ordered_sum`.
