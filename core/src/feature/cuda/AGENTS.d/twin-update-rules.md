---
paths:
  - core/src/feature/cuda/integer_psnr_cuda.c
  - core/src/feature/cuda/integer_ciede_cuda.c
invariant: Cross-backend twin parity table and synchronised update requirements.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# Twin-update rules

Every TU here has at least one cross-backend twin.
Change to one twin **must** ship with matching change(s) in
same PR:

| Feature | Twins |
| --- | --- |
| **psnr** | `integer_psnr_cuda.c` ↔ `../sycl/integer_psnr_sycl.cpp` ↔ `../vulkan/psnr_vulkan.c` (+ `psnr.comp`) ↔ `../hip/integer_psnr_hip.c` |
| **ciede** | `integer_ciede_cuda.c` ↔ `../sycl/integer_ciede_sycl.cpp` ↔ `../vulkan/ciede_vulkan.c` (+ `ciede.comp`) ↔ `../hip/ciede_hip.c` |
| **moment** | `integer_moment_cuda.c` ↔ `../sycl/integer_moment_sycl.cpp` ↔ `../vulkan/moment_vulkan.c` (+ `moment.comp`) ↔ `../hip/float_moment_hip.c` |
| **motion** | `integer_motion_cuda.c` (+ shared `integer_motion_sad_cuda.c`, ADR-1372) ↔ `../sycl/integer_motion_sycl.cpp` ↔ `../vulkan/motion_vulkan.c` (+ `motion.comp`) |
| **motion_v2** | `integer_motion_v2_cuda.c` ↔ `../sycl/integer_motion_v2_sycl.cpp` ↔ `../vulkan/motion_v2_vulkan.c` (+ `motion_v2.comp`) ↔ `../hip/integer_motion_v2_hip.c` |
| **vif (integer)** | `integer_vif_cuda.c` (+ `integer_vif/filter1d.cu`) ↔ `../sycl/integer_vif_sycl.cpp` ↔ `../vulkan/vif_vulkan.c` (+ `vif.comp`) |
| **adm (integer)** | `integer_adm_cuda.c` (+ `integer_adm/*.cu`) ↔ `../sycl/integer_adm_sycl.cpp` ↔ `../vulkan/adm_vulkan.c` (+ `adm.comp`) |
| **ssim (float)** | `integer_ssim_cuda.c` (misnomer; provides `"float_ssim"` — 11-tap float Gaussian) ↔ `../sycl/integer_ssim_sycl.cpp` (float_ssim part) ↔ `../vulkan/ssim_vulkan.c` (+ `ssim.comp`) |
| **ssim (integer)** | `ssim_cuda.c` (real integer_ssim; provides `"ssim"` — 9-tap int64) ↔ `../hip/integer_ssim_hip.c` ↔ `../sycl/integer_ssim_sycl.cpp` (integer_ssim part) — Vulkan integer_ssim pending (ADR-0564) |
| **ms_ssim** | `integer_ms_ssim_cuda.c` ↔ `../sycl/integer_ms_ssim_sycl.cpp` ↔ `../vulkan/ms_ssim_vulkan.c` (+ `ms_ssim.comp`) |
| **psnr_hvs** | `integer_psnr_hvs_cuda.c` ↔ `../sycl/integer_psnr_hvs_sycl.cpp` ↔ `../vulkan/psnr_hvs_vulkan.c` (+ `psnr_hvs.comp`) |
| **ssimulacra2** | `ssimulacra2_cuda.c` (+ `ssimulacra2/*.cu`) ↔ `../sycl/ssimulacra2_sycl.cpp` ↔ `../vulkan/ssimulacra2_vulkan.c` (+ `ssimulacra2_*.comp`) |
| **float_*** | `float_adm_cuda.c` / `float_motion_cuda.c` / `float_psnr_cuda.c` / `float_vif_cuda.c` ↔ matching `../sycl/float_*_sycl.cpp` ↔ partial `../hip/float_*_hip.c` (`float_ansnr_cuda.c` and its twins removed in commit 70ed8b3ce3 / PR #38) |
| **cambi** | `integer_cambi_cuda.c` (+ `integer_cambi/cambi_score.cu`) ↔ `../sycl/integer_cambi_sycl.cpp` ↔ `../hip/integer_cambi_hip.c` ↔ `../metal/integer_cambi_metal.mm` — CUDA + SYCL device-resident (ADR-1379 / ADR-1357); HIP + Metal still Strategy II hybrid. |

Full GPU twin matrix governed by GPU long-tail batches:
[ADR-0182](../../../../../docs/adr/0182-gpu-long-tail-batch-1.md) (psnr /
ciede / moment), [ADR-0188](../../../../../docs/adr/0188-gpu-long-tail-batch-2.md)
(ssim / ms_ssim / psnr_hvs), [ADR-0192](../../../../../docs/adr/0192-gpu-long-tail-batch-3.md)
(motion_v2 / float-twins / ssimulacra2 / cambi; `float_ansnr` removed
in commit 70ed8b3ce3 / PR #38).
