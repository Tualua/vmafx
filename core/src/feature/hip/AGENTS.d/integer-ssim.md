---
paths:
  - core/src/feature/hip/integer_ssim_hip.c
  - core/src/feature/hip/integer_ssim_hip.h
  - core/src/feature/hip/integer_ssim/integer_ssim_score.hip
invariant: Integer SSIM satisfies the CPU contract and identical window accumulation.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# Integer SSIM CPU contract (ADR-0564)

`integer_ssim_hip` publishes canonical `"ssim"` feature, carries
`VMAF_FEATURE_EXTRACTOR_HIP` -> model-driven dispatch under `--backend hip`
runs it instead of CPU `ssim`. Acceptable only while it computes what
`integer_ssim.c::calc_ssim()` computes. ADR-0564 rules out drift under this
name. Before int64 port: twin ran 11-tap float Gaussian, 4.5e-3 off CPU, had to
stay unflagged (ADR-1154).

Invariants:

- **Same kernel as CPU.** 9 integer taps `[2,9,28,55,68,55,28,9,2]`, int64
  moments, boundary *truncation*: taps outside frame skipped, weight counts
  in-bounds taps only. Do not mirror or clamp at border as VIF kernels do
  (ADR-1103); CPU does neither here.
- **Same per-pixel expression.** `issim_cpu_term(issim_factors())` =
  `ssim_reduce_row_range()` operand for operand. `SSIM_K1` / `SSIM_K2`
  spelled `(0.01 * 0.01)` / `(0.03 * 0.03)`; literals `0.0001` / `0.0009` are
  different doubles. `hip_strict_fp_args` builds kernel with
  `-ffp-contract=off` -> nothing fuses into FMA.
- **Same frame sum, on the host (ADR-1438, `EXACT_TWINS`).** One pass-2
  kernel, `integer_ssim_vert_terms`: term of pixel (x, y) stored at
  `terms[y * width + x]`, never added on device. `collect()` adds the plane
  in ascending index = `calc_ssim()` raster order -> score = CPU double at
  every frame size (gfx1036: 178 of 178 frames, `enable_db` / `clip_db` too).
  No block / wave / per-row term reduction, no identical-window shortcut
  (`f.lum_num == f.lum_den` forcing the weight): each gives another double.
  Was: block tree above 4096 pixels (ADR-1400, superseded), up to 1.1e-11 off.
  Cost: 8 bytes per pixel readback, +6 % per 1080p frame, +4 % per 4K frame.
- **Weights: integer, reduced per block.** Shared-memory tree over all 128
  threads, not `warpSize` shuffles -> same on wave32 (RDNA) and wave64 (GCN /
  CDNA). Tree sized for 16x8 launch: `ISSIM_BLOCK_X/Y` in kernel and
  `ISSIM_HIP_BLOCK_X/Y` in host change together.
- Guards: `test_hip_ssim_parity` (+ `_10bit`, `_large`, `_odd`; `==`),
  `test_hip_ssim_tiny_frames` (`==` in dB, 1x1 to 322x182, 8 to 16 bit),
  `test_hip_kernel_source_contract.py` (six planted regressions).
- **Wait for picture upload.** `submit()` gets both luma planes from the
  context's shared frame (waiting upload inside); see "Picture uploads" below.
  Race first seen in this twin: off by up to 0.2 on Netflix 576x324 pair.
