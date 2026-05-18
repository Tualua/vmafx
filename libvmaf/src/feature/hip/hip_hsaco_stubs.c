/**
 *  Copyright 2026 Lusoris and Claude (Anthropic)
 *  SPDX-License-Identifier: BSD-3-Clause-Plus-Patent
 *
 *  ADR-0536: weak stubs for HIP HSACO blob symbols whose .hip kernel
 *  sources don't yet build standalone via `hipcc --genco`.
 *
 *  Background: ADR-0533 (PR #1292) wired the integer ADM HIP extractor
 *  into the dispatch table and the host TU (`integer_adm_hip.c`) loads
 *  four kernel modules (adm_dwt2 / adm_csf / adm_csf_den / adm_cm) via
 *  `hipModuleLoadData(..., adm_*_hsaco)`.  But the four .hip files in
 *  `feature/hip/integer_adm/` still reference CUDA-specific types
 *  (`uint64_cu`, `cta_size`, `warp_reduce`, `VMAF_CUDA_THREADS_PER_WARP`)
 *  that don't compile under `hipcc --genco`'s minimal include set.
 *
 *  Until the kernels are ported off the CUDA helper macros, this TU
 *  provides weak fallback symbols so the host link succeeds.  The host
 *  TU's `hipModuleLoadData` call against an empty blob will fail and
 *  the init path will return -ENOSYS, falling through to the CPU ADM.
 *
 *  Once any individual ADM kernel is ported, register it in the
 *  `hip_kernel_sources` meson dict — the real `<name>_hsaco` definition
 *  from the xxd-embedded blob will override these weak stubs at link time.
 */

#include <stddef.h>

#define VMAF_HSACO_WEAK_STUB(name)                                                                 \
    __attribute__((weak)) const unsigned char name[1] = {0};                                       \
    __attribute__((weak)) const unsigned int name##_len = 0u;

/* ADM kernels (original ADR-0536). */
VMAF_HSACO_WEAK_STUB(adm_dwt2_hsaco)
VMAF_HSACO_WEAK_STUB(adm_csf_hsaco)
VMAF_HSACO_WEAK_STUB(adm_csf_den_hsaco)
VMAF_HSACO_WEAK_STUB(adm_cm_hsaco)

/* SSIMULACRA2 kernels — needed by ssimulacra2_hip.c (ADR-0533). */
VMAF_HSACO_WEAK_STUB(ssimulacra2_blur_hsaco)
VMAF_HSACO_WEAK_STUB(ssimulacra2_mul_hsaco)

/* Other extractors that hipModuleLoadData but whose .hip kernels
 * don't yet build standalone. Each is overridden by the real
 * xxd-embedded blob when its kernel is added to hip_kernel_sources. */
/* float_vif_score_hsaco — real blob now built from
 * feature/hip/float_vif/float_vif_score.hip (ADR-0539). */
VMAF_HSACO_WEAK_STUB(integer_ssim_score_hsaco)
VMAF_HSACO_WEAK_STUB(integer_moment_score_hsaco)
VMAF_HSACO_WEAK_STUB(ms_ssim_score_hsaco)
VMAF_HSACO_WEAK_STUB(psnr_score_hsaco)
VMAF_HSACO_WEAK_STUB(cambi_score_hsaco)
VMAF_HSACO_WEAK_STUB(vif_statistics_hsaco)
