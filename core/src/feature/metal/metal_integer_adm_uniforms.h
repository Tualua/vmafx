/**
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: EUPL-1.2
 *
 *  The uniforms and the reduction layout of integer_adm_metal, one definition
 *  for the kernels (integer_adm.metal), the host (integer_adm_metal_host.c,
 *  integer_adm_metal.mm) and the host replay of the kernels
 *  (core/test/test_metal_integer_adm_host_replay.c). Written on
 *  metal_portable.h, so it compiles as Metal Shading Language and as C or C++.
 *
 *  Every shift and rounding term of IadmCsf is the CPU's: the host copies them
 *  from the contexts integer_adm.c computes with (adm_cm_ctx_init(),
 *  i4_adm_cm_ctx_init(), adm_csf_den_ctx_init(), i4_adm_csf_den_ctx_init(),
 *  i4_dwt2_round() of integer_adm_kernels.h) instead of keeping a table of
 *  its own (T-METAL-INTEGER-ADM-TWIN-DEFECTS-2026-10-05).
 *
 *  Each reduction threadgroup (one band of one row) stores nine 64-bit sums,
 *  each as a low and a high uint32 word: slots 0-2 the denominator, 3-5 the
 *  DLM numerator and 6-8 the AIM numerator of bands h, v and d. The kernels
 *  write and the host reads them through vmaf_mtl_iadm_accum_word() only.
 */

#ifndef VMAF_FEATURE_METAL_METAL_INTEGER_ADM_UNIFORMS_H_
#define VMAF_FEATURE_METAL_METAL_INTEGER_ADM_UNIFORMS_H_

#include "metal_portable.h"

/* 64-bit sums each reduction threadgroup stores. */
#define VMAF_MTL_IADM_ACCUM_SLOTS 9u
/* First slot of the denominator, DLM and AIM groups. */
#define VMAF_MTL_IADM_SLOT_DEN 0u
#define VMAF_MTL_IADM_SLOT_CM 3u
#define VMAF_MTL_IADM_SLOT_AIM 6u

/* Geometry of one scale. */
// NOLINTNEXTLINE(modernize-use-using): C and MSL share this header, ADR-1498
typedef struct IadmDims {
    vmaf_mtl_i32 scale;
    vmaf_mtl_i32 cur_w;
    vmaf_mtl_i32 cur_h;
    vmaf_mtl_i32 half_w;
    vmaf_mtl_i32 half_h;
    vmaf_mtl_i32 buf_stride;
    vmaf_mtl_i32 parent_buf_stride;
    vmaf_mtl_u32 bpc;
} IadmDims;

/* Fixed-point terms of one scale. */
// NOLINTNEXTLINE(modernize-use-using): C and MSL share this header, ADR-1498
typedef struct IadmCsf {
    /* adm_border() of the scale: the region of every reduction. */
    vmaf_mtl_i32 active_left;
    vmaf_mtl_i32 active_top;
    vmaf_mtl_i32 active_right;
    vmaf_mtl_i32 active_bottom;
    /* CSF weights: AdmCmCtx::i_rfactor (scale 0), I4AdmCmCtx::rfactor (1-3). */
    vmaf_mtl_u32 i_rfactor_h;
    vmaf_mtl_u32 i_rfactor_v;
    vmaf_mtl_u32 i_rfactor_d;
    /* adm_enhn_gain_limit as adm_gain_limit_split() returns it. */
    vmaf_mtl_u32 gain_m_hi;
    vmaf_mtl_u32 gain_m_lo;
    vmaf_mtl_i32 gain_frac_bits;
    /* Scale-0 DWT: vertical rounding by the input depth, horizontal 2^16. */
    vmaf_mtl_i32 v_shift;
    vmaf_mtl_i32 v_add_shift;
    vmaf_mtl_i32 h_shift;
    vmaf_mtl_i32 h_add_shift;
    /* Scales 1-3 DWT: i4_dwt2_round(). */
    vmaf_mtl_i32 s123_vert_add;
    vmaf_mtl_i32 s123_vert_shift;
    vmaf_mtl_i32 s123_hori_add;
    vmaf_mtl_i32 s123_hori_shift;
    /* Scales 1-3 CSF and masking terms: I4AdmCmCtx's add_bef_shift_dst /
     * shift_dst and add_bef_shift_flt / shift_flt. The flt rounding term is
     * INT32_MIN, not 2^31 (i4_adm_round_terms(), Netflix#955, ADR-0155). */
    vmaf_mtl_i32 i4_add_shift_dst;
    vmaf_mtl_u32 i4_shift_dst;
    vmaf_mtl_i32 i4_add_shift_flt;
    vmaf_mtl_u32 i4_shift_flt;
    /* Denominator: I4AdmDenCtx's square and cube rounding (scales 1-3) and
     * the row fold of AdmDenCtx / I4AdmDenCtx. */
    vmaf_mtl_u32 den_shift_sq;
    vmaf_mtl_u32 den_add_shift_sq;
    vmaf_mtl_u32 den_shift_cub;
    vmaf_mtl_u32 den_add_shift_cub;
    vmaf_mtl_u32 den_shift_accum;
    vmaf_mtl_u32 den_add_shift_accum;
    /* Contrast masking: AdmCmBand of each band and the row fold. */
    vmaf_mtl_i32 cm_shift_sq[3];
    vmaf_mtl_i32 cm_add_shift_sq[3];
    vmaf_mtl_u32 cm_shift_cub[3];
    vmaf_mtl_u32 cm_add_shift_cub[3];
    vmaf_mtl_i32 cm_shift_sub[3];
    vmaf_mtl_u32 cm_shift_inner;
    vmaf_mtl_u32 cm_add_shift_inner;
} IadmCsf;

/* The uint32 word that holds the low (`hi` 0) or high (`hi` 1) half of slot
 * `slot` of reduction threadgroup `wg`. The words of `wg_count` threadgroups
 * are vmaf_mtl_iadm_accum_word(wg_count, 0, 0). (`half` is an MSL type.) */
VMAF_MTL_FUNC vmaf_mtl_u32 vmaf_mtl_iadm_accum_word(vmaf_mtl_u32 wg, vmaf_mtl_u32 slot,
                                                    vmaf_mtl_u32 hi)
{
    return (((wg * VMAF_MTL_IADM_ACCUM_SLOTS) + slot) * 2u) + hi;
}

#endif /* VMAF_FEATURE_METAL_METAL_INTEGER_ADM_UNIFORMS_H_ */
