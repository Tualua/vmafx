<!-- markdownlint-disable MD013 MD060 -->
# Research-1437: Which HIP twins return the CPU extractor's bits — a sweep of every gate feature on a gfx1036

- **Status**: Active
- **Workstream**: [ADR-1437](../adr/1437-hip-exact-twins-declared.md), [ADR-1421](../adr/1421-rc3-rc8-candidate-map.md)
- **Last updated**: 2026-10-01

## Question

RC3 asks every GPU twin to return the CPU extractor's bits, or to differ only
by the math library with a derived bound
([ADR-1421](../adr/1421-rc3-rc8-candidate-map.md)). On HIP three twins were
declared exact (`adm`, `float_motion`, `psnr_hvs`). For the others:
which already return the CPU's bits, on which inputs, and how far are the
rest?

## Sources

- `origin/master` 80c5a0332, HIP-only release build (`-Denable_hip=true
  -Denable_hipcc=true -Dhip_gfx_targets=gfx1036 -Db_lto=false`), gcc 16.2.1,
  ROCm 7.2.4, AMD gfx1036 (the integrated GPU of a Ryzen 9 9950X3D), Linux
  7.2.8-1-cachyos, 2026-10-01. Other sessions shared the host.
- Every key of `FEATURE_METRICS` in
  `scripts/ci/cross_backend_parity_gate.py` (20 features, all with a HIP
  twin), run through that gate's command builder with `--precision max`,
  `--backend cpu` against `--backend hip` with the twin named, and its
  frame diff with tolerance 0 over every output both sides emit, not only the
  outputs the gate lists.
- Fixtures of the first table: the Netflix 576x324 pair at 8 bits (48 frames)
  and 10 bits (3), the two 1920x1080 checkerboard pairs (3 each), Sparks
  480x270 at 10 bits (5), and the first 48 frames of BBB 3840x2160.
- Fixtures of the second table: the Netflix pair at 12 and 16 bits (3 frames
  each) and as 10-bit 4:2:2 (48), two frames of independent full-range noise
  at 576x324 and 8, 10, 12 and 16 bits (3 frames each), and a bright 16-bit
  1920x1080 pair (samples 56000 to 64000, 2 frames). The noise and the bright
  pair were generated for this sweep to reach the cases real content does
  not: large differences in every block, and sums of large squares.
- A cell that differed was run twice more. Every such cell returned the same
  values in all three runs, so each difference below is arithmetic. No run of
  the sweep showed the device's lost stream commands
  (`T-HIP-GFX1036-DROPPED-DISPATCHES-2026-10-01`): 511 HIP runs.

## Findings

### Frames whose HIP output equals the CPU's, typical content

Cells are identical frames / frames.

| Feature | Output | Netflix 8 bit (48) | Checker 1 px (3) | Checker 10 px (3) | Netflix 10 bit (3) | Sparks 10 bit (5) | BBB 4K (48) | All | Max abs diff |
|---|---|---|---|---|---|---|---|---|---|
| `vif` | `integer_vif_scale0` | 4/48 | 2/3 | 3/3 | 1/3 | 0/5 | 5/48 | 15/110 | 2.4e-07 |
| `vif` | `integer_vif_scale1` | 0/48 | 3/3 | 3/3 | 0/3 | 0/5 | 3/48 | 9/110 | 3.6e-07 |
| `vif` | `integer_vif_scale2` | 1/48 | 2/3 | 3/3 | 0/3 | 3/5 | 5/48 | 14/110 | 3.6e-07 |
| `vif` | `integer_vif_scale3` | 1/48 | 3/3 | 3/3 | 0/3 | 1/5 | 3/48 | 11/110 | 5.4e-07 |
| `motion` | `VMAF_integer_feature_motion_sad_score` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `motion` | `integer_motion2` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `motion` | `integer_motion3` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `motion_debug` | `VMAF_integer_feature_motion_sad_score` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `motion_debug` | `integer_motion` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `motion_debug` | `integer_motion2` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `motion_debug` | `integer_motion3` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `motion_v2` | `VMAF_integer_feature_motion2_v2_score` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `motion_v2` | `VMAF_integer_feature_motion3_v2_score` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `motion_v2` | `VMAF_integer_feature_motion_v2_sad_score` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `adm` | `integer_adm2` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `adm` | `integer_adm_scale0` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `adm` | `integer_adm_scale1` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `adm` | `integer_adm_scale2` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `adm` | `integer_adm_scale3` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `psnr` | `psnr_cb` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `psnr` | `psnr_cr` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `psnr` | `psnr_y` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `float_moment` | `float_moment_dis1st` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `float_moment` | `float_moment_dis2nd` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `float_moment` | `float_moment_ref1st` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `float_moment` | `float_moment_ref2nd` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `ciede` | `ciede2000` | 0/48 | 0/3 | 0/3 | 0/3 | 0/5 | 0/48 | 0/110 | 1.1e-05 |
| `ssim` | `ssim` | 0/48 | 0/3 | 0/3 | 0/3 | 0/5 | 0/48 | 0/110 | 1.1e-11 |
| `float_ssim` | `float_ssim` | 7/48 | 0/3 | 0/3 | 2/3 | 0/5 | 6/48 | 15/110 | 4.8e-07 |
| `float_ssim_lcs` | `float_ssim` | 7/48 | 0/3 | 0/3 | 2/3 | 0/5 | 6/48 | 15/110 | 4.8e-07 |
| `float_ssim_lcs` | `float_ssim_c` | 0/48 | 3/3 | 3/3 | 0/3 | 4/5 | 10/48 | 20/110 | 5.4e-07 |
| `float_ssim_lcs` | `float_ssim_l` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `float_ssim_lcs` | `float_ssim_s` | 0/48 | 0/3 | 0/3 | 0/3 | 0/5 | 9/48 | 9/110 | 5.4e-07 |
| `float_ms_ssim` | `float_ms_ssim` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim_c_scale0` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim_c_scale1` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim_c_scale2` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim_c_scale3` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim_c_scale4` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim_l_scale0` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim_l_scale1` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim_l_scale2` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim_l_scale3` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim_l_scale4` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim_s_scale0` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim_s_scale1` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim_s_scale2` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim_s_scale3` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim_s_scale4` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `float_psnr` | `float_psnr` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `float_motion` | `motion` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `float_motion` | `motion2` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `float_motion` | `motion3` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `float_vif` | `vif_scale0` | 0/48 | 0/3 | 1/3 | 0/3 | 0/5 | 0/48 | 1/110 | 5.8e-06 |
| `float_vif` | `vif_scale1` | 0/48 | 0/3 | 3/3 | 0/3 | 0/5 | 0/48 | 3/110 | 8.4e-06 |
| `float_vif` | `vif_scale2` | 0/48 | 0/3 | 3/3 | 0/3 | 0/5 | 0/48 | 3/110 | 1.9e-05 |
| `float_vif` | `vif_scale3` | 0/48 | 0/3 | 3/3 | 0/3 | 0/5 | 0/48 | 3/110 | 3.8e-05 |
| `psnr_hvs` | `psnr_hvs` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `psnr_hvs` | `psnr_hvs_cb` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `psnr_hvs` | `psnr_hvs_cr` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `psnr_hvs` | `psnr_hvs_y` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |
| `float_adm` | `adm2` | 1/48 | 0/3 | 0/3 | 0/3 | 0/5 | 0/48 | 1/110 | 1.9e-06 |
| `float_adm` | `adm3` | 0/48 | 0/3 | 0/3 | 0/3 | 0/5 | 0/48 | 0/110 | 1.6e-06 |
| `float_adm` | `adm_scale0` | 12/48 | 0/3 | 0/3 | 1/3 | 2/5 | 36/48 | 51/110 | 1.3e-05 |
| `float_adm` | `adm_scale1` | 7/48 | 0/3 | 0/3 | 0/3 | 0/5 | 5/48 | 12/110 | 2.5e-06 |
| `float_adm` | `adm_scale2` | 15/48 | 0/3 | 2/3 | 0/3 | 2/5 | 10/48 | 29/110 | 2.6e-06 |
| `float_adm` | `adm_scale3` | 31/48 | 2/3 | 2/3 | 1/3 | 4/5 | 11/48 | 51/110 | 1.9e-07 |
| `float_adm` | `aim` | 0/48 | 0/3 | 0/3 | 0/3 | 1/5 | 0/48 | 1/110 | 1.3e-06 |
| `ssimulacra2` | `ssimulacra2` | 0/48 | 0/3 | 0/3 | 0/3 | 0/5 | 0/48 | 0/110 | 7.6e-11 |
| `cambi` | `cambi` | 48/48 | 3/3 | 3/3 | 3/3 | 5/5 | 48/48 | 110/110 | 0 |

`adm_hip` emits no `integer_adm3` / `integer_aim`
(`T-GPU-ADM-AIM-DEVICE-PASS-MISSING-SYCL-HIP-2026-09-05`).

### High bit depths, 4:2:2 and stress content

| Feature | Output | Netflix 12 bit (3) | Netflix 16 bit (3) | Netflix 4:2:2 10 bit (48) | Noise 8 bit (3) | Noise 10 bit (3) | Noise 12 bit (3) | Noise 16 bit (3) | Bright 16 bit 1080p (2) | All | Max abs diff |
|---|---|---|---|---|---|---|---|---|---|---|---|
| `vif` | `integer_vif_scale0` | 1/3 | 1/3 | 4/48 | 0/3 | 0/3 | 0/3 | 0/3 | 0/2 | 6/68 | 1.5e-07 |
| `vif` | `integer_vif_scale1` | 0/3 | 0/3 | 0/48 | 0/3 | 0/3 | 0/3 | 0/3 | 0/2 | 0/68 | 2.4e-07 |
| `vif` | `integer_vif_scale2` | 0/3 | 0/3 | 1/48 | 0/3 | 1/3 | 0/3 | 0/3 | 1/2 | 3/68 | 3.6e-07 |
| `vif` | `integer_vif_scale3` | 0/3 | 0/3 | 1/48 | 0/3 | 0/3 | 0/3 | 0/3 | 1/2 | 2/68 | 5.4e-07 |
| `motion` | `VMAF_integer_feature_motion_sad_score` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `motion` | `integer_motion2` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `motion` | `integer_motion3` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `motion_debug` | `VMAF_integer_feature_motion_sad_score` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `motion_debug` | `integer_motion` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `motion_debug` | `integer_motion2` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `motion_debug` | `integer_motion3` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `motion_v2` | `VMAF_integer_feature_motion2_v2_score` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `motion_v2` | `VMAF_integer_feature_motion3_v2_score` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `motion_v2` | `VMAF_integer_feature_motion_v2_sad_score` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `adm` | `integer_adm2` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `adm` | `integer_adm_scale0` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `adm` | `integer_adm_scale1` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `adm` | `integer_adm_scale2` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `adm` | `integer_adm_scale3` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `psnr` | `psnr_cb` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `psnr` | `psnr_cr` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `psnr` | `psnr_y` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `float_moment` | `float_moment_dis1st` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `float_moment` | `float_moment_dis2nd` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 0/3 | 0/2 | 63/68 | 1.0e-04 |
| `float_moment` | `float_moment_ref1st` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `float_moment` | `float_moment_ref2nd` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 0/3 | 0/2 | 63/68 | 1.0e-04 |
| `ciede` | `ciede2000` | 0/3 | 0/3 | 0/48 | 0/3 | 0/3 | 0/3 | 0/3 | 0/2 | 0/68 | 1.1e-05 |
| `ssim` | `ssim` | 0/3 | 0/3 | 1/48 | 0/3 | 0/3 | 0/3 | 0/3 | 0/2 | 1/68 | 3.5e-14 |
| `float_ssim` | `float_ssim` | 2/3 | 2/3 | 7/48 | 0/3 | 0/3 | 0/3 | 0/3 | 1/2 | 12/68 | 2.4e-07 |
| `float_ssim_lcs` | `float_ssim` | 2/3 | 2/3 | 7/48 | 0/3 | 0/3 | 0/3 | 0/3 | 1/2 | 12/68 | 2.4e-07 |
| `float_ssim_lcs` | `float_ssim_c` | 0/3 | 0/3 | 0/48 | 3/3 | 3/3 | 3/3 | 3/3 | 0/2 | 12/68 | 5.4e-07 |
| `float_ssim_lcs` | `float_ssim_l` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `float_ssim_lcs` | `float_ssim_s` | 0/3 | 0/3 | 0/48 | 0/3 | 0/3 | 0/3 | 0/3 | 0/2 | 0/68 | 5.4e-07 |
| `float_ms_ssim` | `float_ms_ssim` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim_c_scale0` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim_c_scale1` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim_c_scale2` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim_c_scale3` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim_c_scale4` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim_l_scale0` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim_l_scale1` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim_l_scale2` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim_l_scale3` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim_l_scale4` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim_s_scale0` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim_s_scale1` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim_s_scale2` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim_s_scale3` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `float_ms_ssim_lcs` | `float_ms_ssim_s_scale4` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `float_psnr` | `float_psnr` | 3/3 | 3/3 | 48/48 | 3/3 | 0/3 | 0/3 | 0/3 | 0/2 | 57/68 | 7.6e-08 |
| `float_motion` | `motion` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `float_motion` | `motion2` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `float_motion` | `motion3` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |
| `float_vif` | `vif_scale0` | 0/3 | 0/3 | 0/48 | 0/3 | 0/3 | 0/3 | 0/3 | 0/2 | 0/68 | 4.2e-06 |
| `float_vif` | `vif_scale1` | 0/3 | 0/3 | 0/48 | 0/3 | 0/3 | 0/3 | 0/3 | 0/2 | 0/68 | 1.1e-04 |
| `float_vif` | `vif_scale2` | 0/3 | 0/3 | 0/48 | 0/3 | 0/3 | 0/3 | 0/3 | 0/2 | 0/68 | 4.0e-05 |
| `float_vif` | `vif_scale3` | 0/3 | 0/3 | 0/48 | 0/3 | 0/3 | 0/3 | 0/3 | 0/2 | 0/68 | 3.8e-05 |
| `psnr_hvs` | `psnr_hvs` | 3/3 | n/a | 48/48 | 3/3 | 3/3 | 3/3 | n/a | n/a | 60/60 | 0 |
| `psnr_hvs` | `psnr_hvs_cb` | 3/3 | n/a | 48/48 | 3/3 | 3/3 | 3/3 | n/a | n/a | 60/60 | 0 |
| `psnr_hvs` | `psnr_hvs_cr` | 3/3 | n/a | 48/48 | 3/3 | 3/3 | 3/3 | n/a | n/a | 60/60 | 0 |
| `psnr_hvs` | `psnr_hvs_y` | 3/3 | n/a | 48/48 | 3/3 | 3/3 | 3/3 | n/a | n/a | 60/60 | 0 |
| `float_adm` | `adm2` | 0/3 | 0/3 | 1/48 | 0/3 | 0/3 | 0/3 | 1/3 | 2/2 | 4/68 | 4.8e-07 |
| `float_adm` | `adm3` | 0/3 | 0/3 | 0/48 | 0/3 | 0/3 | 0/3 | 0/3 | 0/2 | 0/68 | 2.4e-07 |
| `float_adm` | `adm_scale0` | 1/3 | 1/3 | 12/48 | 0/3 | 0/3 | 2/3 | 0/3 | 2/2 | 18/68 | 1.5e-07 |
| `float_adm` | `adm_scale1` | 0/3 | 0/3 | 7/48 | 0/3 | 1/3 | 0/3 | 1/3 | 2/2 | 11/68 | 2.5e-06 |
| `float_adm` | `adm_scale2` | 0/3 | 0/3 | 15/48 | 0/3 | 1/3 | 0/3 | 0/3 | 2/2 | 18/68 | 1.8e-07 |
| `float_adm` | `adm_scale3` | 1/3 | 1/3 | 31/48 | 3/3 | 3/3 | 1/3 | 3/3 | 2/2 | 45/68 | 1.9e-07 |
| `float_adm` | `aim` | 0/3 | 0/3 | 0/48 | 0/3 | 0/3 | 0/3 | 0/3 | 0/2 | 0/68 | 1.0e-07 |
| `ssimulacra2` | `ssimulacra2` | 0/3 | 0/3 | 0/48 | 0/3 | 0/3 | 0/3 | 0/3 | 0/2 | 0/68 | 4.1e-12 |
| `cambi` | `cambi` | 3/3 | 3/3 | 48/48 | 3/3 | 3/3 | 3/3 | 3/3 | 2/2 | 68/68 | 0 |

`psnr_hvs` refuses 16-bit input on the CPU and on the twin.

### What each result is

| Twin | Result | Why |
|---|---|---|
| `motion_hip` (`motion`, `motion_debug`), `motion_v2_hip` | Identical on all 178 frames | Integer SAD on the device; the host weights and blends through the CPU's helpers (ADR-1377, ADR-1382) |
| `psnr_hip` | Identical on all 178 frames, three planes | Integer SSE in `uint64`; the host concludes through `psnr_score.h` (ADR-1382) |
| `cambi_hip` | Identical on all 178 frames | Integer pipeline; the top-K sum is exact fixed point and the host combine is `cambi.c`'s helpers (ADR-1378) |
| `integer_ms_ssim_hip` (`float_ms_ssim`, with and without `enable_lcs`) | Identical on all 178 frames, 16 outputs | The CPU's arithmetic type for type (ADR-1403). The per-scale sums are fp64 in another order than the CPU's and are rounded to fp32, which absorbs the order: see the caveat below |
| `adm_hip`, `float_motion_hip`, `psnr_hvs_hip` | Identical (already declared) | ADR-1423, ADR-1419, ADR-1401 |
| `vif_hip` | 49 of 440 scores on typical content | The kernel evaluates `log2f()` on the device where the CPU reads a table built with the host library: 77 of 32768 values differ by one. Fixed on `fix/hip-vif-cpu-log2-table` (#1768), which uploads the CPU's table |
| `float_psnr_hip` | Identical on typical content and at 8 bits; up to 7.6e-8 dB off on the 10-, 12- and 16-bit stress frames | Each 16x16 block is summed in fp32. A squared difference at depth `b` is a multiple of `4^(8 - b)`, so the block sum is exact while its value in that unit stays below 2^24: always at 8 bits, and at 10, 12 and 16 bits only while the block's rms difference is below 256 code values. `T-HIP-FLOAT-PSNR-FP32-BLOCK-SUMS-2026-10-01` |
| `float_moment_hip` | Identical at 8, 10 and 12 bits; second moments up to 1.0e-4 off at 16 bits | The device adds exact integer squares. `moment.c` rounds each square to `float` first, which is exact up to 12 bits (the square has at most 24 bits) and rounds at 16. `T-HIP-FLOAT-MOMENT-16BIT-SQUARES-2026-10-01` |
| `integer_ssim_hip` | No frame; up to 1.1e-11 | Known: the twin adds the per-pixel terms per block above 4096 pixels, the CPU in raster order into one double (`T-GPU-SSIM-FRAME-SUM-ORDER-2026-10-01`) |
| `float_ssim_hip` | 15 of 110 scores, up to 4.8e-7; with `enable_lcs`, `float_ssim_l` identical on every frame, `_c` and `_s` up to 5.4e-7 off | The luminance term needs the window means only; the contrast and structure terms need the window sums of squares, which `iqa_convolve()` adds in fp64 and the kernel in fp32. The MS-SSIM twin had the same defect (ADR-1403). `T-HIP-FLOAT-SSIM-NOT-CPU-ARITHMETIC-2026-10-01` |
| `float_vif_hip` | 10 of 440 scores; up to 3.8e-5 on typical content and 1.1e-4 on the bright 16-bit pair, which is above the gate's 5e-5 | Known: a stale tap table, the device `log2`, an fp32 `vif_sigma_nsq` and per-block sums (`T-GPU-FLOAT-VIF-CPU-ARITHMETIC-2026-10-01`; the numbers here are its first HIP measurement) |
| `float_adm_hip` | 145 of 770 values; up to 1.3e-5 | Not analysed in this sweep. `T-HIP-FLOAT-ADM-NOT-CPU-ARITHMETIC-2026-10-01` |
| `ciede_hip` | No frame; up to 1.1e-5 | Known: fp32 arithmetic in another form of the formula (`T-GPU-CIEDE-CPU-ARITHMETIC-2026-10-01`; first HIP measurement) |
| `ssimulacra2_hip` | No frame; up to 7.6e-11 | Its per-scale sums are exact fp32 pairs over a fixed tree where the CPU adds in fp64 in raster order (ADR-1390 gives the twin a 1e-9 contract). `T-HIP-SSIMULACRA2-NOT-CPU-BITS-2026-10-01` |

### The caveat on `float_ms_ssim`

`ssim_accumulate_default_scalar()` adds `l`, `c` and `s` of every window into
three doubles, in raster order, and `iqa_ssim()` returns each mean as a
`float`. The HIP twin (and the SYCL twin, ADR-1414) adds the same per-window
values in another order and rounds the mean to `float` the same way. Two
double sums of the same N terms in different orders differ by rounding of the
order of `sqrt(N) * 2^-53` of their value (the random-walk estimate), about
3e-13 for the 8.3 million windows of a 3840x2160 scale; the mean's `float`
rounding has a step of 6e-8 to 1.2e-7 of it. The two round to different
floats only when the sum lies that close to a rounding boundary: by this
estimate a few means in a million. It is an estimate, not a measurement. The
CPU's own AVX2 and AVX-512 accumulators add in yet another order and rely on
the same rounding. No mean differed on the 178 frames here (2848 values), nor
on the 107 frames of ADR-1403.

## Method notes

- The gate's own listing was not enough: `psnr` lists `psnr_y` only, and
  the twins emit more. The sweep compares every output both sides emit and
  reports an output only one side has.
- The repository's 10-, 12- and 16-bit Netflix fixtures are the 8-bit clip
  shifted left: every sample has zero low bits. They exercise no arithmetic
  an 8-bit frame does not, which is why they agree wherever the 8-bit pair
  agrees.
- 8-bit natural content does not separate "exact by construction" from "exact
  on this input". The noise frames and the 16-bit frames did:
  `float_psnr_hip` and `float_moment_hip` are identical on all 110 frames of
  the first table and are not exact twins.

## Open questions

- Why `float_adm_hip` differs, per stage.
- Whether `ssimulacra2_hip` can round its sums as the CPU's sequential sums
  do (the CUDA twin does since ADR-1433, at twice the frame time) at a cost
  that is acceptable on this device.
