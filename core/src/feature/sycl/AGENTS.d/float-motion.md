---
paths:
  - core/src/feature/sycl/float_motion_sycl.cpp
  - core/test/test_sycl_float_motion_parity.c
invariant: float_motion_sycl.cpp SAD = CPU order, bit for bit; emits through motion_clip().
---
<!-- markdownlint-disable MD013 MD060 -->
# Float motion extractor and kernels

- **`float_motion_sycl.cpp` emits through `motion_clip()`** (ADR-1365).
  Every emitted `motion` / `motion2` (debug score, tail in `flush()`
  included) = `MIN(score * motion_fps_weight, motion_max_val)`, CPU
  `float_motion.c` order (min of two SADs first). `motion_force_zero`
  short-circuits `submit()` (no upload, no kernel) and `collect()` emits
  zeros; before ADR-1365 it was declared and ignored. `motion3` not
  provided (CPU extractor only).
- **`float_motion_sycl.cpp` SAD = CPU order, bit for bit (ADR-1409,
  ADR-1411).** `float_motion.c::compute_motion_simd()` = one fp32 running
  sum per row, one fp32 sum over rows, fp32 division. Twin:
  `launch_float_motion_row_sad()` = ONE work-item per row
  (`sycl::range<1>(height)`, sub-group size 16), `fm_row_sad()` = plain
  `for (j = 0; j < width; j++)` loop, readback `height` floats; host =
  `vmaf_float_motion_score_from_row_sads()` (`../float_motion_sad.h`). No
  group / sub-group / atomic reduction in the TU, no host sum in
  `collect()`: any other shape differs in low bits (was 1.36e-4 on 1080p
  checkerboards). Blur kernel writes blur only; blur =
  `convolution_f32_c_s()` tap order, needs the strict FP line (ADR-1367).
  CPU SAD order changes upstream -> change kernel + helper in same PR.
  Arc A380: Netflix pair 48 / 48, checkerboards 3 / 3, BBB 4K 200 / 200
  identical (`motion`, `motion2`), also 10 / 12 / 16 bit. Cost: row pass
  re-reads both blurred planes, 0.70 ms per 4K frame (old reduction 0.34):
  3.85 -> 4.23 ms / frame. Sub-group 8 measured fastest (16: 0.82, 32:
  1.12); `select_from_group` chain over 16 consecutive pixels 1.10,
  lane-uniform 16-wide chain 0.80: do not retry without a new idea.
  Scratch-free (ADR-1395). `EXACT_TWINS` lists `float_motion`: `sycl`.
  Guards: `test_sycl_float_motion_parity` (+ `_large`, `==`, 8 / 10 / 12
  bit), `test_sycl_kernel_source_contract.py` (five planted regressions).

| SYCL TU | CPU TU | Parity test | ADR |
|---|---|---|---|
| `float_motion_sycl.cpp` | `float_motion.c` | `test_sycl_float_motion_parity.c` (bit-exact, every frame, 8 / 10 / 12 bit) | ADR-0946 (round 3), ADR-1411 |
