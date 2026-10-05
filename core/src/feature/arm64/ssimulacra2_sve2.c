/**
 *
 *  Copyright (c) the JPEG XL Project Authors.
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent AND BSD-3-Clause
 */

/*
 * aarch64 SVE2 port of the SSIMULACRA 2 SIMD kernels (T7-38). Matches
 * the NEON sibling lane-for-lane: every kernel processes 4 float lanes
 * at a time under an `svwhilelt_b32(0, 4)` fixed-width predicate so the
 * arithmetic order is identical to `float32x4_t` regardless of the
 * runtime vector length. This preserves the ADR-0138 / ADR-0139 /
 * ADR-0140 byte-exact contract: the SVE2 path produces output that is
 * memcmp-equal to both NEON and the scalar reference.
 *
 * Tails (loop bound n % 4 != 0) are handled by tightening the predicate
 * via `svwhilelt_b32(i, n)`. All `cbrtf` / `srgb_eotf` libm calls stay
 * scalar (per-lane spill + reload) — same as the NEON port.
 *
 * Research-0016 / Research-0017 captured the design path; this TU
 * supersedes the "deferred pending CI hardware" footnote — local
 * validation runs under qemu-aarch64-static with `-cpu max,sve=on,
 * sve2=on`.
 */

#include <arm_neon.h>
#include <arm_sve.h>
#include <assert.h>
#include <math.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "feature/ssimulacra2_math.h"
#include "feature/ssimulacra2_score.h"
#include "ssimulacra2_sve2.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#endif
#pragma STDC FP_CONTRACT OFF
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include "ssimulacra2_arm64_common.h"

static const float kC2 = 0.0009f;

/* All kernels lock to 4 active lanes (`PG4`) to mirror NEON arithmetic
 * order exactly. The runtime vector length on SVE2 hardware is always
 * >= 128 bits (4 floats), so this is universally safe. The wider
 * lanes simply stay false in the predicate. */
static inline svbool_t pg4(void)
{
    return svwhilelt_b32((uint32_t)0, (uint32_t)4);
}

static inline svfloat32_t cbrtf_lane_sve2(svbool_t pg, svfloat32_t v)
{
    alignas(16) float tmp[4] = {0.f, 0.f, 0.f, 0.f};
    svst1_f32(pg, tmp, v);
    for (int k = 0; k < 4; k++) {
        tmp[k] = vmaf_ss2_cbrtf(tmp[k]);
    }
    return svld1_f32(pg, tmp);
}

void ssimulacra2_multiply_3plane_sve2(const float *a, const float *b, float *mul, unsigned w,
                                      unsigned h)
{
    const size_t n = 3u * (size_t)w * (size_t)h;
    const svbool_t pg = pg4();
    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        const svfloat32_t va = svld1_f32(pg, a + i);
        const svfloat32_t vb = svld1_f32(pg, b + i);
        svst1_f32(pg, mul + i, svmul_f32_x(pg, va, vb));
    }
    for (; i < n; i++) {
        mul[i] = a[i] * b[i];
    }
}

/* Four pixels: the SVE2 form of ss2_xyb_block_neon(), the same operations on
 * the same constants (`Ss2XybK` scalars) under the four-lane predicate. */
static inline void ss2_xyb_block_sve2(const Ss2XybK *k, svbool_t pg, const float *rp,
                                      const float *gp, const float *bp, float *xp, float *yp,
                                      float *bxp)
{
    const svfloat32_t vbias = svdup_f32(kOpsinBias);
    const svfloat32_t vzero = svdup_f32(0.0f);
    const svfloat32_t vcbrt_bias = svdup_f32(k->cbrt_bias);
    const svfloat32_t r = svld1_f32(pg, rp);
    const svfloat32_t g = svld1_f32(pg, gp);
    const svfloat32_t b = svld1_f32(pg, bp);
    svfloat32_t l =
        svadd_f32_x(pg, svmul_f32_x(pg, svdup_f32(kM00), r), svmul_f32_x(pg, svdup_f32(k->m01), g));
    l = svadd_f32_x(pg, l, svmul_f32_x(pg, svdup_f32(kM02), b));
    l = svadd_f32_x(pg, l, vbias);
    svfloat32_t m =
        svadd_f32_x(pg, svmul_f32_x(pg, svdup_f32(kM10), r), svmul_f32_x(pg, svdup_f32(k->m11), g));
    m = svadd_f32_x(pg, m, svmul_f32_x(pg, svdup_f32(kM12), b));
    m = svadd_f32_x(pg, m, vbias);
    svfloat32_t sv =
        svadd_f32_x(pg, svmul_f32_x(pg, svdup_f32(kM20), r), svmul_f32_x(pg, svdup_f32(kM21), g));
    sv = svadd_f32_x(pg, sv, svmul_f32_x(pg, svdup_f32(k->m22), b));
    sv = svadd_f32_x(pg, sv, vbias);
    l = svmax_f32_x(pg, l, vzero);
    m = svmax_f32_x(pg, m, vzero);
    sv = svmax_f32_x(pg, sv, vzero);
    const svfloat32_t lc = svsub_f32_x(pg, cbrtf_lane_sve2(pg, l), vcbrt_bias);
    const svfloat32_t mc = svsub_f32_x(pg, cbrtf_lane_sve2(pg, m), vcbrt_bias);
    const svfloat32_t sc = svsub_f32_x(pg, cbrtf_lane_sve2(pg, sv), vcbrt_bias);
    const svfloat32_t vhalf = svdup_f32(0.5f);
    const svfloat32_t x = svmul_f32_x(pg, vhalf, svsub_f32_x(pg, lc, mc));
    const svfloat32_t y = svmul_f32_x(pg, vhalf, svadd_f32_x(pg, lc, mc));
    svst1_f32(pg, xp, svadd_f32_x(pg, svmul_f32_x(pg, x, svdup_f32(14.0f)), svdup_f32(0.42f)));
    svst1_f32(pg, yp, svadd_f32_x(pg, y, svdup_f32(0.01f)));
    svst1_f32(pg, bxp, svadd_f32_x(pg, svsub_f32_x(pg, sc, y), svdup_f32(0.55f)));
}

void ssimulacra2_linear_rgb_to_xyb_sve2(const float *lin, float *xyb, unsigned w, unsigned h)
{
    assert(lin != NULL);
    assert(xyb != NULL);
    assert(w > 0 && h > 0);
    const size_t plane_sz = (size_t)w * (size_t)h;
    const float *rp = lin;
    const float *gp = lin + plane_sz;
    const float *bp = lin + 2 * plane_sz;
    float *xp = xyb;
    float *yp = xyb + plane_sz;
    float *bxp = xyb + 2 * plane_sz;
    const Ss2XybK k = ss2_xyb_k_init();
    const svbool_t pg = pg4();

    size_t i = 0;
    for (; i + 4 <= plane_sz; i += 4) {
        ss2_xyb_block_sve2(&k, pg, rp + i, gp + i, bp + i, xp + i, yp + i, bxp + i);
    }
    for (; i < plane_sz; i++) {
        ss2_xyb_pixel(&k, rp[i], gp[i], bp[i], xp + i, yp + i, bxp + i);
    }
}

/* One output row: the SVE2 form of ss2_downsample_row_2x2_neon(). The NEON
 * deinterleave stays (the audited reference); the adds are SVE2 under the
 * four-lane predicate, in the scalar's order. */
static inline void ss2_downsample_row_2x2_sve2(const float *row0, const float *row1, float *orow,
                                               unsigned iw, unsigned ow)
{
    const svbool_t pg = pg4();
    const svfloat32_t vquarter = svdup_f32(0.25f);
    const unsigned interior_end = (ow > 0 && iw >= 2) ? ((ow - 1) / 4) * 4 : 0;
    unsigned ox = 0;
    for (; ox < interior_end; ox += 4) {
        const size_t base = (size_t)ox * 2u;
        const float32x4_t r00 = vld1q_f32(row0 + base);
        const float32x4_t r01 = vld1q_f32(row0 + base + 4);
        const float32x4_t r10 = vld1q_f32(row1 + base);
        const float32x4_t r11 = vld1q_f32(row1 + base + 4);
        const float32x4_t r0e = vuzp1q_f32(r00, r01);
        const float32x4_t r0o = vuzp2q_f32(r00, r01);
        const float32x4_t r1e = vuzp1q_f32(r10, r11);
        const float32x4_t r1o = vuzp2q_f32(r10, r11);
        const svfloat32_t s_r0e = svld1_f32(pg, (const float *)&r0e);
        const svfloat32_t s_r0o = svld1_f32(pg, (const float *)&r0o);
        const svfloat32_t s_r1e = svld1_f32(pg, (const float *)&r1e);
        const svfloat32_t s_r1o = svld1_f32(pg, (const float *)&r1o);
        svfloat32_t acc = svadd_f32_x(pg, s_r0e, s_r0o);
        acc = svadd_f32_x(pg, acc, s_r1e);
        acc = svadd_f32_x(pg, acc, s_r1o);
        svst1_f32(pg, orow + ox, svmul_f32_x(pg, acc, vquarter));
    }
    for (; ox < ow; ox++) {
        const unsigned ix0 = ox * 2;
        const unsigned ix1 = (ix0 + 1 < iw) ? ix0 + 1 : iw - 1;
        const float sum = row0[ix0] + row0[ix1] + row1[ix0] + row1[ix1];
        orow[ox] = sum * 0.25f;
    }
}

void ssimulacra2_downsample_2x2_sve2(const float *in, unsigned iw, unsigned ih, float *out,
                                     unsigned *ow_out, unsigned *oh_out)
{
    const unsigned ow = (iw + 1) / 2;
    const unsigned oh = (ih + 1) / 2;
    *ow_out = ow;
    *oh_out = oh;

    const size_t in_plane = (size_t)iw * (size_t)ih;
    const size_t out_plane = (size_t)ow * (size_t)oh;

    for (int c = 0; c < 3; c++) {
        const float *ip = in + (size_t)c * in_plane;
        float *op = out + (size_t)c * out_plane;
        for (unsigned oy = 0; oy < oh; oy++) {
            const unsigned iy0 = oy * 2;
            const unsigned iy1 = (iy0 + 1 < ih) ? iy0 + 1 : ih - 1;
            ss2_downsample_row_2x2_sve2(ip + (size_t)iy0 * iw, ip + (size_t)iy1 * iw,
                                        op + (size_t)oy * ow, iw, ow);
        }
    }
}

void ssimulacra2_ssim_map_sve2(const float *m1, const float *m2, const float *s11, const float *s22,
                               const float *s12, unsigned w, unsigned h, double plane_averages[6])
{
    const size_t plane = (size_t)w * (size_t)h;
    const double one_per_pixels = 1.0 / (double)plane;
    const svbool_t pg = pg4();
    const svfloat32_t vc2 = svdup_f32(kC2);
    const svfloat32_t vone = svdup_f32(1.0f);
    const svfloat32_t vtwo = svdup_f32(2.0f);

    for (int c = 0; c < 3; c++) {
        Ss2SsimSums sums = {0.0, 0.0};
        const float *rm1 = m1 + (size_t)c * plane;
        const float *rm2 = m2 + (size_t)c * plane;
        const float *rs11 = s11 + (size_t)c * plane;
        const float *rs22 = s22 + (size_t)c * plane;
        const float *rs12 = s12 + (size_t)c * plane;

        size_t i = 0;
        for (; i + 4 <= plane; i += 4) {
            const svfloat32_t mu1 = svld1_f32(pg, rm1 + i);
            const svfloat32_t mu2 = svld1_f32(pg, rm2 + i);
            const svfloat32_t mu11 = svmul_f32_x(pg, mu1, mu1);
            const svfloat32_t mu22 = svmul_f32_x(pg, mu2, mu2);
            const svfloat32_t mu12 = svmul_f32_x(pg, mu1, mu2);
            const svfloat32_t diff = svsub_f32_x(pg, mu1, mu2);
            const svfloat32_t num_m = svsub_f32_x(pg, vone, svmul_f32_x(pg, diff, diff));
            const svfloat32_t num_s = svadd_f32_x(
                pg, svmul_f32_x(pg, vtwo, svsub_f32_x(pg, svld1_f32(pg, rs12 + i), mu12)), vc2);
            const svfloat32_t denom_s =
                svadd_f32_x(pg,
                            svadd_f32_x(pg, svsub_f32_x(pg, svld1_f32(pg, rs11 + i), mu11),
                                        svsub_f32_x(pg, svld1_f32(pg, rs22 + i), mu22)),
                            vc2);
            alignas(16) float num_m_f[4] = {0.f, 0.f, 0.f, 0.f};
            alignas(16) float num_s_f[4] = {0.f, 0.f, 0.f, 0.f};
            alignas(16) float denom_s_f[4] = {0.f, 0.f, 0.f, 0.f};
            svst1_f32(pg, num_m_f, num_m);
            svst1_f32(pg, num_s_f, num_s);
            svst1_f32(pg, denom_s_f, denom_s);
            for (int k = 0; k < 4; k++) {
                ss2_ssim_accumulate(&sums, num_m_f[k], num_s_f[k], denom_s_f[k]);
            }
        }
        for (; i < plane; i++) {
            float mu1 = rm1[i];
            float mu2 = rm2[i];
            float mu11 = mu1 * mu1;
            float mu22 = mu2 * mu2;
            float mu12 = mu1 * mu2;
            float num_m = 1.0f - (mu1 - mu2) * (mu1 - mu2);
            float num_s = 2.0f * (rs12[i] - mu12) + kC2;
            float denom_s = (rs11[i] - mu11) + (rs22[i] - mu22) + kC2;
            ss2_ssim_accumulate(&sums, num_m, num_s, denom_s);
        }
        plane_averages[c * 2 + 0] = one_per_pixels * sums.l1;
        plane_averages[c * 2 + 1] = sqrt(sqrt(one_per_pixels * sums.l4));
    }
}

void ssimulacra2_edge_diff_map_sve2(const float *img1, const float *mu1, const float *img2,
                                    const float *mu2, unsigned w, unsigned h,
                                    double plane_averages[12])
{
    const size_t plane = (size_t)w * (size_t)h;
    const double one_per_pixels = 1.0 / (double)plane;
    const svbool_t pg = pg4();

    for (int c = 0; c < 3; c++) {
        Ss2EdgeSums sums = {0.0, 0.0, 0.0, 0.0};
        const float *r1 = img1 + (size_t)c * plane;
        const float *rm1 = mu1 + (size_t)c * plane;
        const float *r2 = img2 + (size_t)c * plane;
        const float *rm2 = mu2 + (size_t)c * plane;

        size_t i = 0;
        for (; i + 4 <= plane; i += 4) {
            const svfloat32_t a1 = svld1_f32(pg, r1 + i);
            const svfloat32_t a2 = svld1_f32(pg, r2 + i);
            const svfloat32_t am1 = svld1_f32(pg, rm1 + i);
            const svfloat32_t am2 = svld1_f32(pg, rm2 + i);
            alignas(16) float a1f[4] = {0.f, 0.f, 0.f, 0.f};
            alignas(16) float am1f[4] = {0.f, 0.f, 0.f, 0.f};
            alignas(16) float a2f[4] = {0.f, 0.f, 0.f, 0.f};
            alignas(16) float am2f[4] = {0.f, 0.f, 0.f, 0.f};
            svst1_f32(pg, a1f, a1);
            svst1_f32(pg, am1f, am1);
            svst1_f32(pg, a2f, a2);
            svst1_f32(pg, am2f, am2);
            for (int k = 0; k < 4; k++) {
                ss2_edge_accumulate(&sums, a1f[k], am1f[k], a2f[k], am2f[k]);
            }
        }
        for (; i < plane; i++) {
            ss2_edge_accumulate(&sums, r1[i], rm1[i], r2[i], rm2[i]);
        }
        ss2_edge_store(plane_averages, c, one_per_pixels, &sums);
    }
}

/* The poles' coefficients; an SVE vector cannot be a struct member or an
 * array element, so they stay scalars and are broadcast where they are used. */
typedef struct {
    float n2[3];
    float d1[3];
} Ss2IirKSve2;

static inline Ss2IirKSve2 ss2_iir_k_init_sve2(const float rg_n2[3], const float rg_d1[3])
{
    Ss2IirKSve2 k;
    for (int p = 0; p < 3; p++) {
        k.n2[p] = rg_n2[p];
        k.d1[p] = rg_d1[p];
    }
    return k;
}

/* One pole of the recursive Gaussian, n2 * sum - d1 * prev1 - prev2 in the
 * scalar's order, over four state lanes in memory: `prev2` takes the old
 * `prev1`, `prev1` takes the result, which is returned. */
static inline svfloat32_t ss2_iir_pole_sve2(const Ss2IirKSve2 *k, svbool_t pg, int p,
                                            svfloat32_t sum, float *prev1, float *prev2)
{
    const svfloat32_t p1 = svld1_f32(pg, prev1);
    const svfloat32_t p2 = svld1_f32(pg, prev2);
    const svfloat32_t o = svsub_f32_x(pg, svmul_f32_x(pg, svdup_f32(k->n2[p]), sum),
                                      svmul_f32_x(pg, svdup_f32(k->d1[p]), p1));
    const svfloat32_t res = svsub_f32_x(pg, o, p2);
    svst1_f32(pg, prev2, p1);
    svst1_f32(pg, prev1, res);
    return res;
}

/* Lane i = row_bases[i][idx] for i < row_count, zero elsewhere. The byte-exact
 * contract pins the lane-by-lane assembly of the NEON port (SVE2 has gather). */
static inline svfloat32_t ss2_hblur_gather_sve2(svbool_t pg, const float *const row_bases[4],
                                                unsigned row_count, ptrdiff_t idx)
{
    alignas(16) float lane_tmp[4] = {0.f, 0.f, 0.f, 0.f};
    for (unsigned i = 0; i < 4; i++) {
        lane_tmp[i] = (i < row_count) ? row_bases[i][idx] : 0.f;
    }
    return svld1_f32(pg, lane_tmp);
}

/* Three poles of the IIR over one input sum: advances the state rows and
 * returns (o0 + o1) + o2. */
static inline svfloat32_t ss2_iir_step3_sve2(const Ss2IirKSve2 *k, svbool_t pg, svfloat32_t sum,
                                             float prev1[3][4], float prev2[3][4])
{
    const svfloat32_t o0 = ss2_iir_pole_sve2(k, pg, 0, sum, prev1[0], prev2[0]);
    const svfloat32_t o1 = ss2_iir_pole_sve2(k, pg, 1, sum, prev1[1], prev2[1]);
    const svfloat32_t o2 = ss2_iir_pole_sve2(k, pg, 2, sum, prev1[2], prev2[2]);
    return svadd_f32_x(pg, svadd_f32_x(pg, o0, o1), o2);
}

static void hblur_4rows_sve2(const float rg_n2[3], const float rg_d1[3], int rg_radius,
                             const float *in, float *out, unsigned w, unsigned y_base,
                             unsigned row_count)
{
    const ptrdiff_t N = (ptrdiff_t)rg_radius;
    const ptrdiff_t W = (ptrdiff_t)w;
    const svbool_t pg = pg4();
    const Ss2IirKSve2 k = ss2_iir_k_init_sve2(rg_n2, rg_d1);
    alignas(16) float prev1[3][4] = {{0.f}};
    alignas(16) float prev2[3][4] = {{0.f}};
    alignas(16) float store_tmp[4] = {0.f, 0.f, 0.f, 0.f};

    /* Per-lane row base pointers. */
    /* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
     * C23, where clang-tidy also proposes the `nullptr` keyword, but MSVC's
     * documented /std:clatest C23 feature set does not include `nullptr` and the
     * required Windows builds compile C with cl.exe (C2065). ADR-1138. */
    const float *row_bases[4] = {NULL, NULL, NULL, NULL};
    /* NOLINTEND(modernize-use-nullptr) */
    for (unsigned i = 0; i < row_count && i < 4; i++) {
        row_bases[i] = in + ((size_t)y_base + i) * w;
    }

    for (ptrdiff_t n = -N + 1; n < W; n++) {
        const ptrdiff_t left = n - N - 1;
        const ptrdiff_t right = n + N - 1;
        const svfloat32_t lv =
            (left >= 0) ? ss2_hblur_gather_sve2(pg, row_bases, row_count, left) : svdup_f32(0.f);
        const svfloat32_t rv =
            (right < W) ? ss2_hblur_gather_sve2(pg, row_bases, row_count, right) : svdup_f32(0.f);
        const svfloat32_t res = ss2_iir_step3_sve2(&k, pg, svadd_f32_x(pg, lv, rv), prev1, prev2);

        if (n >= 0) {
            svst1_f32(pg, store_tmp, res);
            for (unsigned i = 0; i < row_count; i++) {
                out[((size_t)y_base + i) * w + (size_t)n] = store_tmp[i];
            }
        }
    }
}

/* The six per-column IIR state rows of the vertical blur. */
typedef struct {
    float *prev1[3];
    float *prev2[3];
} Ss2ColStateSve2;

/* Four columns at x: the rows above and below (NULL outside the image) are
 * summed, the state rows advance and, with an output row, (o0 + o1) + o2 is
 * stored. */
static inline void ss2_vblur_block_sve2(const Ss2IirKSve2 *k, svbool_t pg,
                                        const Ss2ColStateSve2 *st, const float *lrow,
                                        const float *rrow, float *orow, size_t x)
{
    const svfloat32_t lv = lrow ? svld1_f32(pg, lrow + x) : svdup_f32(0.f);
    const svfloat32_t rv = rrow ? svld1_f32(pg, rrow + x) : svdup_f32(0.f);
    const svfloat32_t sum = svadd_f32_x(pg, lv, rv);
    const svfloat32_t o0 = ss2_iir_pole_sve2(k, pg, 0, sum, st->prev1[0] + x, st->prev2[0] + x);
    const svfloat32_t o1 = ss2_iir_pole_sve2(k, pg, 1, sum, st->prev1[1] + x, st->prev2[1] + x);
    const svfloat32_t o2 = ss2_iir_pole_sve2(k, pg, 2, sum, st->prev1[2] + x, st->prev2[2] + x);
    if (orow) {
        svst1_f32(pg, orow + x, svadd_f32_x(pg, svadd_f32_x(pg, o0, o1), o2));
    }
}

/* One column: the scalar tail of ss2_vblur_block_sve2(), same operations. */
static inline void ss2_vblur_pixel_sve2(const float rg_n2[3], const float rg_d1[3],
                                        const Ss2ColStateSve2 *st, const float *lrow,
                                        const float *rrow, float *orow, size_t x)
{
    const float lv = lrow ? lrow[x] : 0.f;
    const float rv = rrow ? rrow[x] : 0.f;
    const float sum = lv + rv;
    const float o0 = rg_n2[0] * sum - rg_d1[0] * st->prev1[0][x] - st->prev2[0][x];
    const float o1 = rg_n2[1] * sum - rg_d1[1] * st->prev1[1][x] - st->prev2[1][x];
    const float o2 = rg_n2[2] * sum - rg_d1[2] * st->prev1[2][x] - st->prev2[2][x];
    for (int p = 0; p < 3; p++) {
        st->prev2[p][x] = st->prev1[p][x];
    }
    st->prev1[0][x] = o0;
    st->prev1[1][x] = o1;
    st->prev1[2][x] = o2;
    if (orow) {
        orow[x] = o0 + o1 + o2;
    }
}

static void vblur_simd_4cols_sve2(const float rg_n2[3], const float rg_d1[3], int rg_radius,
                                  float *col_state, const float *in, float *out, unsigned w,
                                  unsigned h)
{
    const size_t xsize = (size_t)w;
    Ss2ColStateSve2 st;
    for (int p = 0; p < 3; p++) {
        st.prev1[p] = col_state + (size_t)p * xsize;
        st.prev2[p] = col_state + (size_t)(p + 3) * xsize;
    }
    memset(col_state, 0, 6u * xsize * sizeof(float));

    const svbool_t pg = pg4();
    const Ss2IirKSve2 k = ss2_iir_k_init_sve2(rg_n2, rg_d1);
    const ptrdiff_t N = (ptrdiff_t)rg_radius;
    const ptrdiff_t ysize = (ptrdiff_t)h;

    for (ptrdiff_t n = -N + 1; n < ysize; n++) {
        const ptrdiff_t left = n - N - 1;
        const ptrdiff_t right = n + N - 1;
        /* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
         * C23, where clang-tidy also proposes the `nullptr` keyword, but MSVC's
         * documented /std:clatest C23 feature set does not include `nullptr` and the
         * required Windows builds compile C with cl.exe (C2065). ADR-1138. */
        const float *lrow = (left >= 0) ? (in + (size_t)left * xsize) : NULL;
        const float *rrow = (right < ysize) ? (in + (size_t)right * xsize) : NULL;
        float *orow = (n >= 0) ? (out + (size_t)n * xsize) : NULL;
        /* NOLINTEND(modernize-use-nullptr) */

        size_t x = 0;
        for (; x + 4 <= xsize; x += 4) {
            ss2_vblur_block_sve2(&k, pg, &st, lrow, rrow, orow, x);
        }
        for (; x < xsize; x++) {
            ss2_vblur_pixel_sve2(rg_n2, rg_d1, &st, lrow, rrow, orow, x);
        }
    }
}

void ssimulacra2_blur_plane_sve2(const float rg_n2[3], const float rg_d1[3], int rg_radius,
                                 float *col_state, const float *in, float *out, float *scratch,
                                 unsigned w, unsigned h)
{
    assert(col_state != NULL);
    assert(in != NULL);
    assert(out != NULL);
    assert(scratch != NULL);
    assert(w > 0 && h > 0);

    unsigned y = 0;
    for (; y + 4 <= h; y += 4) {
        hblur_4rows_sve2(rg_n2, rg_d1, rg_radius, in, scratch, w, y, 4);
    }
    if (y < h) {
        hblur_4rows_sve2(rg_n2, rg_d1, rg_radius, in, scratch, w, y, h - y);
    }
    vblur_simd_4cols_sve2(rg_n2, rg_d1, rg_radius, col_state, scratch, out, w, h);
}

/* YUV → linear RGB (ADR-0163). 4-wide aarch64 SVE2 mirror of the NEON
 * port. Per-lane scalar reads + per-lane scalar `srgb_eotf` keep
 * byte-exact parity with both the scalar and NEON outputs. */

static inline svfloat32_t srgb_to_linear_lane_sve2(svbool_t pg, svfloat32_t v)
{
    alignas(16) float tmp[4] = {0.f, 0.f, 0.f, 0.f};
    svst1_f32(pg, tmp, v);
    for (int k = 0; k < 4; k++) {
        const float x = tmp[k];
        tmp[k] = vmaf_ss2_srgb_eotf(x);
    }
    return svld1_f32(pg, tmp);
}

/* Four pixels at (x, y): the SVE2 form of ss2_yuv_block_neon(); the samples
 * are read one by one (the planes may be subsampled). */
static inline void ss2_yuv_block_sve2(const Ss2YuvK *k, svbool_t pg, const simd_plane_t planes[3],
                                      unsigned w, unsigned h, unsigned bpc, unsigned x, unsigned y,
                                      float *rdst, float *gdst, float *bdst)
{
    alignas(16) float y_tmp[4] = {0.f, 0.f, 0.f, 0.f};
    alignas(16) float u_tmp[4] = {0.f, 0.f, 0.f, 0.f};
    alignas(16) float v_tmp[4] = {0.f, 0.f, 0.f, 0.f};
    for (int i = 0; i < 4; i++) {
        y_tmp[i] = ss2_read_plane_scalar(&planes[0], w, h, (int)(x + (unsigned)i), (int)y, bpc);
        u_tmp[i] = ss2_read_plane_scalar(&planes[1], w, h, (int)(x + (unsigned)i), (int)y, bpc);
        v_tmp[i] = ss2_read_plane_scalar(&planes[2], w, h, (int)(x + (unsigned)i), (int)y, bpc);
    }
    const svfloat32_t vinv_peak = svdup_f32(k->inv_peak);
    const svfloat32_t vc_off = svdup_f32(k->c_off);
    const svfloat32_t vc_scale = svdup_f32(k->c_scale);
    const svfloat32_t vzero = svdup_f32(0.0f);
    const svfloat32_t vone = svdup_f32(1.0f);
    const svfloat32_t Y = svmul_f32_x(pg, svld1_f32(pg, y_tmp), vinv_peak);
    const svfloat32_t U = svmul_f32_x(pg, svld1_f32(pg, u_tmp), vinv_peak);
    const svfloat32_t V = svmul_f32_x(pg, svld1_f32(pg, v_tmp), vinv_peak);
    const svfloat32_t Yn =
        svmul_f32_x(pg, svsub_f32_x(pg, Y, svdup_f32(k->y_off)), svdup_f32(k->y_scale));
    const svfloat32_t Un = svmul_f32_x(pg, svsub_f32_x(pg, U, vc_off), vc_scale);
    const svfloat32_t Vn = svmul_f32_x(pg, svsub_f32_x(pg, V, vc_off), vc_scale);
    /* ADR-0891: svmla_f32_x — single-rounding FMA matches fmaf() in scalar ref. */
    svfloat32_t R = svmla_f32_x(pg, Yn, svdup_f32(k->cr_r), Vn);
    svfloat32_t G = svmla_f32_x(pg, Yn, svdup_f32(k->cb_g), Un);
    G = svmla_f32_x(pg, G, svdup_f32(k->cr_g), Vn);
    svfloat32_t B = svmla_f32_x(pg, Yn, svdup_f32(k->cb_b), Un);
    R = svmax_f32_x(pg, svmin_f32_x(pg, R, vone), vzero);
    G = svmax_f32_x(pg, svmin_f32_x(pg, G, vone), vzero);
    B = svmax_f32_x(pg, svmin_f32_x(pg, B, vone), vzero);
    svst1_f32(pg, rdst, srgb_to_linear_lane_sve2(pg, R));
    svst1_f32(pg, gdst, srgb_to_linear_lane_sve2(pg, G));
    svst1_f32(pg, bdst, srgb_to_linear_lane_sve2(pg, B));
}

void ssimulacra2_picture_to_linear_rgb_sve2(int yuv_matrix, unsigned bpc, unsigned w, unsigned h,
                                            const simd_plane_t planes[3], float *out)
{
    assert(planes != NULL);
    assert(out != NULL);
    assert(w > 0 && h > 0);

    const size_t plane_sz = (size_t)w * (size_t)h;
    float *rp = out;
    float *gp = out + plane_sz;
    float *bp = out + 2 * plane_sz;
    const Ss2YuvK k = ss2_yuv_k_init(yuv_matrix, bpc);
    const svbool_t pg = pg4();

    for (unsigned y = 0; y < h; y++) {
        unsigned x = 0;
        for (; x + 4 <= w; x += 4) {
            const size_t idx = (size_t)y * w + x;
            ss2_yuv_block_sve2(&k, pg, planes, w, h, bpc, x, y, rp + idx, gp + idx, bp + idx);
        }
        for (; x < w; x++) {
            const size_t idx = (size_t)y * w + x;
            ss2_yuv_pixel(&k, planes, w, h, bpc, x, y, rp + idx, gp + idx, bp + idx);
        }
    }
}
