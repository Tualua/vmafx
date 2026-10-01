/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * The interface between test_sycl_float_adm_math.c and its SYCL probe
 * (test_sycl_float_adm_math_probe.cpp): what float_adm_sycl.cpp's kernels run
 * per work-item (feature/sycl/sycl_float_adm_math.h), callable from C on the
 * host and on the default GPU (ADR-1434).
 */

#ifndef LIBVMAF_TEST_SYCL_FLOAT_ADM_MATH_PROBE_H_
#define LIBVMAF_TEST_SYCL_FLOAT_ADM_MATH_PROBE_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __cplusplus
typedef struct VmafTestFadmScale VmafTestFadmScale;
typedef struct VmafTestFadmSpots VmafTestFadmSpots;
#endif

/* One scale as the three kernels take it. Every band buffer has `w` floats
 * per row and `h` rows per sub-band. */
struct VmafTestFadmScale {
    const float *ref; /* 4 sub-bands: a, h, v, d */
    const float *dis; /* 4 sub-bands */
    int w;
    int h;
    float rfactor[3];  /* adm_csf_rfactor_s() */
    float cos_1deg_sq; /* adm_decouple_cos_1deg_sq_s() */
    double gain_limit; /* adm_enhn_gain_limit */
    double p_norm;     /* adm_p_norm */
    int bypass_cm;     /* adm_bypass_cm */
    int left;          /* the reduced region, adm_border_s() */
    int top;
    int right;
    int bottom;
    float *csf[4]; /* out: csf_a, csf_fa, csf_r, csf_fr, 3 sub-bands each */
    float *rows;   /* out: 9 x (bottom - top) row sums, slot by slot */
};

/* The decouple, term and row-sum kernels over one scale: on the host when
 * `on_device` is 0, else in three kernels on the default GPU, launched as the
 * extractor launches them. 0, -EINVAL, -ENODEV without a device, -EIO on a
 * SYCL error. */
int vmaf_test_sycl_fadm_scale(const VmafTestFadmScale *scale, int on_device);

/* The reference's three fp64 expressions over n samples:
 *   flt[i]    = (float)(FLOAT_ONE_BY_30 * fabsf(a[i]))
 *   centre[i] = (float)((double)sum[i] + FLOAT_ONE_BY_15 * fabsf(a[i]))
 *   gain[i]   = the enhancement-gain clamp of rst[i] (non-zero) against t[i]
 * sum[i] is non-negative. */
struct VmafTestFadmSpots {
    const float *a;
    const float *sum;
    const float *rst;
    const float *t;
    size_t n;
    double gain_limit;
    float *flt;
    float *centre;
    float *gain;
};

/* On the host. `replay_only` 0: the two products with a constant as the
 * kernels select (the pair, and the replay where it is undecided); non-zero:
 * the fp64 replay for every sample. `undecided`, when not NULL, receives the
 * number of samples whose 1/30 product the pair does not decide. */
void vmaf_test_sycl_fadm_spots_host(const VmafTestFadmSpots *spots, int replay_only,
                                    size_t *undecided);

/* In one kernel on the default GPU, as the kernels select. 0, -EINVAL,
 * -ENODEV without a device, -EIO on a SYCL error. */
int vmaf_test_sycl_fadm_spots_device(const VmafTestFadmSpots *spots);

/* The two constants as the kernels hold them: out[0], out[1] = the exact
 * values of FLOAT_ONE_BY_30 and FLOAT_ONE_BY_15 (mant * 2^exp), out[2],
 * out[3] = their fp32 pairs (hi + lo, evaluated in double). */
void vmaf_test_sycl_fadm_constants(double out[4]);

#ifdef __cplusplus
}
#endif

#endif /* LIBVMAF_TEST_SYCL_FLOAT_ADM_MATH_PROBE_H_ */
