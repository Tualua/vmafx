/**
 *
 *  Copyright (c) the JPEG XL Project Authors.
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent AND BSD-3-Clause
 */

/*
 * Helpers shared by `ssimulacra2_neon.c` and `ssimulacra2_host_neon.c`: the
 * linear RGB -> XYB kernel and the 2x2 downsample row (the two files differ
 * only in how a plane pointer is formed from the base). `Ss2XybK` holds the opsin matrix as vectors and the
 * scalars the tail pixels use; `ss2_xyb_block_neon()` is four pixels and
 * `ss2_xyb_pixel()` is one, both the operations of the scalar reference in the
 * same order (ADR-0161 / ADR-0242): left-to-right LMS sums, per-lane scalar
 * cbrtf, no contraction. The includer sets `#pragma STDC FP_CONTRACT OFF`
 * before this header and is built with `-ffp-contract=off`.
 */

#ifndef VMAF_FEATURE_ARM64_SSIMULACRA2_ARM64_COMMON_H_
#define VMAF_FEATURE_ARM64_SSIMULACRA2_ARM64_COMMON_H_

#include <arm_neon.h>
#include <stdalign.h>

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "feature/ssimulacra2_math.h"
#include "feature/ssimulacra2_score.h"
#include "feature/ssimulacra2_simd_common.h"

static const float kM00 = 0.30f;
static const float kM02 = 0.078f;
static const float kM10 = 0.23f;
static const float kM12 = 0.078f;
static const float kM20 = 0.24342268924547819f;
static const float kM21 = 0.20476744424496821f;
static const float kOpsinBias = 0.0037930732552754493f;

typedef struct {
    float32x4_t vm00;
    float32x4_t vm01;
    float32x4_t vm02;
    float32x4_t vm10;
    float32x4_t vm11;
    float32x4_t vm12;
    float32x4_t vm20;
    float32x4_t vm21;
    float32x4_t vm22;
    float32x4_t vbias;
    float32x4_t vzero;
    float32x4_t vcbrt_bias;
    float32x4_t vhalf;
    float32x4_t v14;
    float32x4_t v42;
    float32x4_t v55;
    float32x4_t v01;
    float m01;
    float m11;
    float m22;
    float cbrt_bias;
} Ss2XybK;

static inline Ss2XybK ss2_xyb_k_init(void)
{
    Ss2XybK k;
    k.m01 = 1.0f - kM00 - kM02;
    k.m11 = 1.0f - kM10 - kM12;
    k.m22 = 1.0f - kM20 - kM21;
    k.cbrt_bias = vmaf_ss2_cbrtf(kOpsinBias);
    k.vm00 = vdupq_n_f32(kM00);
    k.vm01 = vdupq_n_f32(k.m01);
    k.vm02 = vdupq_n_f32(kM02);
    k.vm10 = vdupq_n_f32(kM10);
    k.vm11 = vdupq_n_f32(k.m11);
    k.vm12 = vdupq_n_f32(kM12);
    k.vm20 = vdupq_n_f32(kM20);
    k.vm21 = vdupq_n_f32(kM21);
    k.vm22 = vdupq_n_f32(k.m22);
    k.vbias = vdupq_n_f32(kOpsinBias);
    k.vzero = vdupq_n_f32(0.0f);
    k.vcbrt_bias = vdupq_n_f32(k.cbrt_bias);
    k.vhalf = vdupq_n_f32(0.5f);
    k.v14 = vdupq_n_f32(14.0f);
    k.v42 = vdupq_n_f32(0.42f);
    k.v55 = vdupq_n_f32(0.55f);
    k.v01 = vdupq_n_f32(0.01f);
    return k;
}

static inline float32x4_t ss2_cbrtf_lane4(float32x4_t v)
{
    alignas(16) float tmp[4];
    vst1q_f32(tmp, v);
    for (int i = 0; i < 4; i++) {
        tmp[i] = vmaf_ss2_cbrtf(tmp[i]);
    }
    return vld1q_f32(tmp);
}

/* Four pixels: `rp`, `gp`, `bp` and `xp`, `yp`, `bxp` point at the first. */
static inline void ss2_xyb_block_neon(const Ss2XybK *k, const float *rp, const float *gp,
                                      const float *bp, float *xp, float *yp, float *bxp)
{
    const float32x4_t r = vld1q_f32(rp);
    const float32x4_t g = vld1q_f32(gp);
    const float32x4_t b = vld1q_f32(bp);
    /* LMS mixing: left-to-right addition order matches the scalar reference. */
    float32x4_t l = vaddq_f32(vmulq_f32(k->vm00, r), vmulq_f32(k->vm01, g));
    l = vaddq_f32(l, vmulq_f32(k->vm02, b));
    l = vaddq_f32(l, k->vbias);
    float32x4_t m = vaddq_f32(vmulq_f32(k->vm10, r), vmulq_f32(k->vm11, g));
    m = vaddq_f32(m, vmulq_f32(k->vm12, b));
    m = vaddq_f32(m, k->vbias);
    float32x4_t sv = vaddq_f32(vmulq_f32(k->vm20, r), vmulq_f32(k->vm21, g));
    sv = vaddq_f32(sv, vmulq_f32(k->vm22, b));
    sv = vaddq_f32(sv, k->vbias);
    l = vmaxq_f32(l, k->vzero);
    m = vmaxq_f32(m, k->vzero);
    sv = vmaxq_f32(sv, k->vzero);
    const float32x4_t lc = vsubq_f32(ss2_cbrtf_lane4(l), k->vcbrt_bias);
    const float32x4_t mc = vsubq_f32(ss2_cbrtf_lane4(m), k->vcbrt_bias);
    const float32x4_t sc = vsubq_f32(ss2_cbrtf_lane4(sv), k->vcbrt_bias);
    /* X = 0.5*(L-M); Y = 0.5*(L+M); B = (S-Y)+0.55; X = 14X+0.42; Y += 0.01 */
    const float32x4_t x = vmulq_f32(k->vhalf, vsubq_f32(lc, mc));
    const float32x4_t y = vmulq_f32(k->vhalf, vaddq_f32(lc, mc));
    vst1q_f32(xp, vaddq_f32(vmulq_f32(x, k->v14), k->v42));
    vst1q_f32(yp, vaddq_f32(y, k->v01));
    vst1q_f32(bxp, vaddq_f32(vsubq_f32(sc, y), k->v55));
}

/* One pixel, bit-identical to the scalar reference. */
static inline void ss2_xyb_pixel(const Ss2XybK *k, float r, float g, float bb, float *xp, float *yp,
                                 float *bxp)
{
    float l = kM00 * r + k->m01 * g + kM02 * bb + kOpsinBias;
    float m = kM10 * r + k->m11 * g + kM12 * bb + kOpsinBias;
    float s = kM20 * r + kM21 * g + k->m22 * bb + kOpsinBias;
    if (l < 0.0f) {
        l = 0.0f;
    }
    if (m < 0.0f) {
        m = 0.0f;
    }
    if (s < 0.0f) {
        s = 0.0f;
    }
    const float lc = vmaf_ss2_cbrtf(l) - k->cbrt_bias;
    const float mc = vmaf_ss2_cbrtf(m) - k->cbrt_bias;
    const float sc = vmaf_ss2_cbrtf(s) - k->cbrt_bias;
    float x = 0.5f * (lc - mc);
    float y = 0.5f * (lc + mc);
    float bf = sc;
    bf = (bf - y) + 0.55f;
    x = x * 14.0f + 0.42f;
    y = y + 0.01f;
    *xp = x;
    *yp = y;
    *bxp = bf;
}

/* One output row: 4 output lanes at a time through the SIMD interior, then the
 * scalar tail. `vuzp1q_f32` extracts even positions, `vuzp2q_f32` extracts
 * odd, equivalent to the AVX2 shuffle+permute. Sequential adds preserve the
 * scalar summation order. */
static inline void ss2_downsample_row_2x2_neon(const float *row0, const float *row1, float *orow,
                                               unsigned iw, unsigned ow)
{
    const float32x4_t vquarter = vdupq_n_f32(0.25f);
    const unsigned interior_end = (ow > 0u && iw >= 2u) ? (((ow - 1u) / 4u) * 4u) : 0u;
    unsigned ox = 0;
    for (; ox < interior_end; ox += 4) {
        const size_t base = (size_t)ox * 2u;
        const float32x4_t r0a = vld1q_f32(row0 + base);
        const float32x4_t r0b = vld1q_f32(row0 + base + 4);
        const float32x4_t r1a = vld1q_f32(row1 + base);
        const float32x4_t r1b = vld1q_f32(row1 + base + 4);
        /* Deinterleave even / odd sample pairs across r0a:r0b. */
        const float32x4_t r0e = vuzp1q_f32(r0a, r0b);
        const float32x4_t r0o = vuzp2q_f32(r0a, r0b);
        const float32x4_t r1e = vuzp1q_f32(r1a, r1b);
        const float32x4_t r1o = vuzp2q_f32(r1a, r1b);
        /* (r0e + r0o) + r1e + r1o — scalar summation order. */
        float32x4_t acc = vaddq_f32(r0e, r0o);
        acc = vaddq_f32(acc, r1e);
        acc = vaddq_f32(acc, r1o);
        vst1q_f32(orow + ox, vmulq_f32(acc, vquarter));
    }
    /* Scalar tail. */
    for (; ox < ow; ox++) {
        const unsigned ix0 = ox * 2;
        const unsigned ix1 = (ix0 + 1 < iw) ? ix0 + 1 : iw - 1;
        const float sum = row0[ix0] + row0[ix1] + row1[ix0] + row1[ix1];
        orow[ox] = sum * 0.25f;
    }
}

static inline double quartic_d(double x)
{
    x *= x;
    return x * x;
}

typedef struct {
    double l1;
    double l4;
} Ss2SsimSums;

/* One pixel's `1 - num_m * num_s / denom_s` clamped at 0, in double, added to
 * the plane's two running sums in the scalar's order. */
static inline void ss2_ssim_accumulate(Ss2SsimSums *s, float num_m, float num_s, float denom_s)
{
    double d = 1.0 - ((double)num_m * (double)num_s / (double)denom_s);
    if (d < 0.0) {
        d = 0.0;
    }
    s->l1 += d;
    s->l4 += quartic_d(d);
}

typedef struct {
    double s0;
    double s1;
    double s2;
    double s3;
} Ss2EdgeSums;

/* ADR-1208: the reference difference is taken in DOUBLE. `a` and `am` are
 * floats, so `(double)a - (double)am` is exact, whereas subtracting in float
 * rounds first. The scalar `edge_diff_map`, the vector functions' scalar
 * tails and the test's reference all promote before subtracting; vectorising
 * the subtract in float made the ssimulacra2 score depend on whether the host
 * had SIMD. One pixel, the operations in the scalar's order. */
static inline void ss2_edge_accumulate(Ss2EdgeSums *s, float a1, float am1, float a2, float am2)
{
    const double ed1 = fabs((double)a1 - (double)am1);
    const double ed2 = fabs((double)a2 - (double)am2);
    const double d = (1.0 + ed2) / (1.0 + ed1) - 1.0;
    double art;
    double det;
    vmaf_ss2_split_edge_difference(d, &art, &det);
    s->s0 += art;
    s->s1 += quartic_d(art);
    s->s2 += det;
    s->s3 += quartic_d(det);
}

static inline void ss2_edge_store(double *plane_averages, int c, double one_per_pixels,
                                  const Ss2EdgeSums *s)
{
    plane_averages[c * 4 + 0] = one_per_pixels * s->s0;
    plane_averages[c * 4 + 1] = sqrt(sqrt(one_per_pixels * s->s1));
    plane_averages[c * 4 + 2] = one_per_pixels * s->s2;
    plane_averages[c * 4 + 3] = sqrt(sqrt(one_per_pixels * s->s3));
}

static inline float ss2_read_plane_scalar(const simd_plane_t *p, unsigned lw, unsigned lh, int x,
                                          int y, unsigned bpc)
{
    const unsigned pw = p->w;
    const unsigned ph = p->h;
    int sx;
    int sy;
    if (pw == lw) {
        sx = x;
    } else if (pw * 2 == lw) {
        sx = x >> 1;
    } else {
        sx = (int)((int64_t)x * (int64_t)pw / (int64_t)lw);
    }
    if (ph == lh) {
        sy = y;
    } else if (ph * 2 == lh) {
        sy = y >> 1;
    } else {
        sy = (int)((int64_t)y * (int64_t)ph / (int64_t)lh);
    }
    if (sx < 0)
        sx = 0;
    if (sy < 0)
        sy = 0;
    if ((unsigned)sx >= pw)
        sx = (int)pw - 1;
    if ((unsigned)sy >= ph)
        sy = (int)ph - 1;
    if (bpc > 8) {
        const uint16_t *row = (const uint16_t *)((const uint8_t *)p->data + (size_t)sy * p->stride);
        return (float)row[sx];
    }
    const uint8_t *row = (const uint8_t *)p->data + (size_t)sy * p->stride;
    return (float)row[sx];
}

static inline void ss2_matrix_coefs(int yuv_matrix, float *kr_out, float *kg_out, float *kb_out,
                                    int *limited_out)
{
    switch (yuv_matrix) {
    case 2:
        *limited_out = 0;
        *kr_out = 0.2126f;
        *kg_out = 0.7152f;
        *kb_out = 0.0722f;
        break;
    case 0:
        *limited_out = 1;
        *kr_out = 0.2126f;
        *kg_out = 0.7152f;
        *kb_out = 0.0722f;
        break;
    case 3:
        *limited_out = 0;
        *kr_out = 0.299f;
        *kg_out = 0.587f;
        *kb_out = 0.114f;
        break;
    case 1:
    default:
        *limited_out = 1;
        *kr_out = 0.299f;
        *kg_out = 0.587f;
        *kb_out = 0.114f;
        break;
    }
}

typedef struct {
    float inv_peak;
    float y_scale;
    float c_scale;
    float y_off;
    float c_off;
    float cr_r;
    float cb_b;
    float cb_g;
    float cr_g;
} Ss2YuvK;

static inline Ss2YuvK ss2_yuv_k_init(int yuv_matrix, unsigned bpc)
{
    float kr;
    float kg;
    float kb;
    int limited;
    ss2_matrix_coefs(yuv_matrix, &kr, &kg, &kb, &limited);

    Ss2YuvK k;
    k.inv_peak = 1.0f / (float)((1u << bpc) - 1u);
    k.cr_r = 2.0f * (1.0f - kr);
    k.cb_b = 2.0f * (1.0f - kb);
    k.cb_g = -(2.0f * kb * (1.0f - kb)) / kg;
    k.cr_g = -(2.0f * kr * (1.0f - kr)) / kg;
    k.y_scale = limited ? (255.0f / 219.0f) : 1.0f;
    k.c_scale = limited ? (255.0f / 224.0f) : 1.0f;
    k.y_off = limited ? (16.0f / 255.0f) : 0.0f;
    k.c_off = 0.5f;
    return k;
}

static inline float ss2_clamp01(float v)
{
    if (v < 0.0f) {
        v = 0.0f;
    }
    if (v > 1.0f) {
        v = 1.0f;
    }
    return v;
}

/* One pixel at (x, y), bit-identical to the scalar reference. */
static inline void ss2_yuv_pixel(const Ss2YuvK *k, const simd_plane_t planes[3], unsigned w,
                                 unsigned h, unsigned bpc, unsigned x, unsigned y, float *rdst,
                                 float *gdst, float *bdst)
{
    const float Ys = ss2_read_plane_scalar(&planes[0], w, h, (int)x, (int)y, bpc) * k->inv_peak;
    const float Us = ss2_read_plane_scalar(&planes[1], w, h, (int)x, (int)y, bpc) * k->inv_peak;
    const float Vs = ss2_read_plane_scalar(&planes[2], w, h, (int)x, (int)y, bpc) * k->inv_peak;
    const float Yn = (Ys - k->y_off) * k->y_scale;
    const float Un = (Us - k->c_off) * k->c_scale;
    const float Vn = (Vs - k->c_off) * k->c_scale;
    /* ADR-0891: fmaf() matches vfmaq_f32 single-rounding contract. */
    float R = fmaf(k->cr_r, Vn, Yn);
    float G = fmaf(k->cb_g, Un, Yn);
    G = fmaf(k->cr_g, Vn, G);
    float B = fmaf(k->cb_b, Un, Yn);
    R = ss2_clamp01(R);
    G = ss2_clamp01(G);
    B = ss2_clamp01(B);
    *rdst = vmaf_ss2_srgb_eotf(R);
    *gdst = vmaf_ss2_srgb_eotf(G);
    *bdst = vmaf_ss2_srgb_eotf(B);
}

#endif /* VMAF_FEATURE_ARM64_SSIMULACRA2_ARM64_COMMON_H_ */
