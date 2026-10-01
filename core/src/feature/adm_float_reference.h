/**
 *  Copyright 2016-2020 Netflix, Inc.
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 *  The parts of the float ADM reference (adm_tools.c) that a GPU twin runs on
 *  the host instead of re-deriving: the reduced region, the CSF weights, the
 *  pooling of one scale's band accumulators and the decouple's angle
 *  threshold (ADR-1420). The decouple's division needs no export: it is the
 *  IEEE fp32 quotient on every host (ADR-1442).
 *
 *  A twin that computes any of these itself has a second implementation to
 *  keep in step with the reference. Calling them is what makes its result the
 *  reference's.
 */

#ifndef VMAF_SRC_FEATURE_ADM_FLOAT_REFERENCE_H_
#define VMAF_SRC_FEATURE_ADM_FLOAT_REFERENCE_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Half-open region [left, right) x [top, bottom) of one scale's bands. */
typedef struct AdmBorderS {
    int left;
    int top;
    int right;
    int bottom;
} AdmBorderS;

/* The region the reductions run over: `border_factor` of each frame edge is
 * excluded. */
AdmBorderS adm_border_s(int w, int h, double border_factor);

/* CSF weights of DWT scale `scale`: rfactor[0..1] for the (h, v) bands,
 * rfactor[2] for the (d) band. A negative adm_f1sN / adm_f2sN keeps the
 * model's value. */
void adm_csf_rfactor_s(int scale, double adm_norm_view_dist, int adm_ref_display_height,
                       int adm_csf_mode, double luminance_level, double adm_csf_scale,
                       double adm_csf_diag_scale, double adm_f1s0, double adm_f1s1, double adm_f1s2,
                       double adm_f1s3, double adm_f2s0, double adm_f2s1, double adm_f2s2,
                       double adm_f2s3, float rfactor[3]);

/* One scale's value from its (h, v, d) accumulators over a region of
 * region_w x region_h samples: each band's 1 / adm_p_norm root plus the noise
 * floor, added in band order. */
float adm_pool_bands_s(const float accum[3], int region_w, int region_h, double adm_noise_weight,
                       double adm_p_norm);

/* cos(1 degree) squared as the fp32 the decouple's angle test compares with. */
float adm_decouple_cos_1deg_sq_s(void);

#ifdef __cplusplus
}
#endif

#endif /* VMAF_SRC_FEATURE_ADM_FLOAT_REFERENCE_H_ */
