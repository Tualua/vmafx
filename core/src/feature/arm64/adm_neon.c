/**
 *
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 *     Licensed under the BSD+Patent License (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         https://opensource.org/licenses/BSDplusPatent
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */

#include "feature/arm64/adm_neon.h"
#include "feature/integer_adm.h"
#include "feature/integer_adm_kernels.h"

#include <arm_neon.h>

/* Four-tap multiply-accumulate of eight int16 lanes, widened to two int32x4
 * halves: acc = init + f[0] * v[0] + f[1] * v[1] + f[2] * v[2] + f[3] * v[3].
 * The vertical pass feeds it four source rows, the horizontal pass the two
 * de-interleaved halves of two vld2q loads. Integer arithmetic throughout, so
 * the association order cannot perturb the result. */
typedef struct AdmNeonAccum {
    int32x4_t lo;
    int32x4_t hi;
} AdmNeonAccum;

static inline AdmNeonAccum adm_neon_macc4(int32x4_t init, const int16x8_t v[4], int16x4_t filter)
{
    AdmNeonAccum acc;
    acc.lo = vmlal_lane_s16(init, vget_low_s16(v[0]), filter, 0);
    acc.hi = vmlal_high_lane_s16(init, v[0], filter, 0);
    acc.lo = vmlal_lane_s16(acc.lo, vget_low_s16(v[1]), filter, 1);
    acc.hi = vmlal_high_lane_s16(acc.hi, v[1], filter, 1);
    acc.lo = vmlal_lane_s16(acc.lo, vget_low_s16(v[2]), filter, 2);
    acc.hi = vmlal_high_lane_s16(acc.hi, v[2], filter, 2);
    acc.lo = vmlal_lane_s16(acc.lo, vget_low_s16(v[3]), filter, 3);
    acc.hi = vmlal_high_lane_s16(acc.hi, v[3], filter, 3);
    return acc;
}

/* Shift both halves (a negative `shift` is a right shift), narrow them back to
 * int16 by keeping the low half of every lane, and store eight samples. */
static inline void adm_neon_store_shifted(int16_t *out, AdmNeonAccum acc, int32x4_t shift)
{
    const int16x8_t narrowed = vuzp1q_s16(vreinterpretq_s16_s32(vshlq_s32(acc.lo, shift)),
                                          vreinterpretq_s16_s32(vshlq_s32(acc.hi, shift)));
    vst1q_s16(out, narrowed);
}

enum {
    ADM_DWT2_8_SHIFT_VP = 8,
    ADM_DWT2_8_ADD_SHIFT_VP = 128,
    ADM_DWT2_8_SHIFT_HP = 16,
    ADM_DWT2_8_ADD_SHIFT_HP = 32768,
};

/* One vertical-pass column through the scalar arithmetic, for the columns the
 * 16-wide loop cannot reach. */
static void adm_dwt2_8_neon_vpass_column(const uint8_t *const rows[4], int j, int16_t *tmplo,
                                         int16_t *tmphi)
{
    int32_t accum_lo = 0;
    int32_t accum_hi = 0;

    for (int tap = 0; tap < 4; tap++) {
        const int32_t sample = (int32_t)(uint16_t)rows[tap][j];
        accum_lo += (int32_t)dwt2_db2_coeffs_lo[tap] * sample;
        accum_hi += (int32_t)dwt2_db2_coeffs_hi[tap] * sample;
    }
    accum_lo -= (int32_t)dwt2_db2_coeffs_lo_sum * ADM_DWT2_8_ADD_SHIFT_VP;
    accum_hi -= (int32_t)dwt2_db2_coeffs_hi_sum * ADM_DWT2_8_ADD_SHIFT_VP;
    tmplo[j] = (int16_t)((accum_lo + ADM_DWT2_8_ADD_SHIFT_VP) >> ADM_DWT2_8_SHIFT_VP);
    tmphi[j] = (int16_t)((accum_hi + ADM_DWT2_8_ADD_SHIFT_VP) >> ADM_DWT2_8_SHIFT_VP);
}

/* Vertical pass of one output row into tmplo/tmphi (w samples each). */
static void adm_dwt2_8_neon_vpass_row(const uint8_t *const rows[4], int w, int16_t *tmplo,
                                      int16_t *tmphi)
{
    const int16x4_t filter_lo_vec = vld1_s16(dwt2_db2_coeffs_lo);
    const int16x4_t filter_hi_vec = vld1_s16(dwt2_db2_coeffs_hi);
    const int32x4_t normalize_vec_vp_lo = vdupq_n_s32(
        (-1 * (int32_t)dwt2_db2_coeffs_lo_sum * ADM_DWT2_8_ADD_SHIFT_VP) + ADM_DWT2_8_ADD_SHIFT_VP);
    const int32x4_t normalize_vec_vp_hi = vdupq_n_s32(
        (-1 * (int32_t)dwt2_db2_coeffs_hi_sum * ADM_DWT2_8_ADD_SHIFT_VP) + ADM_DWT2_8_ADD_SHIFT_VP);
    const int32x4_t shift_vp_vec = vdupq_n_s32(-ADM_DWT2_8_SHIFT_VP);

    for (int j = 0; j < w - 15; j += 16) {
        int16x8_t s_16_l[4];
        int16x8_t s_16_h[4];

        for (int tap = 0; tap < 4; tap++) {
            const uint8x16_t u_8 = vld1q_u8(rows[tap] + j);
            s_16_l[tap] = vreinterpretq_s16_u16(vmovl_u8(vget_low_u8(u_8)));
            s_16_h[tap] = vreinterpretq_s16_u16(vmovl_high_u8(u_8));
        }

        adm_neon_store_shifted(
            tmplo + j, adm_neon_macc4(normalize_vec_vp_lo, s_16_l, filter_lo_vec), shift_vp_vec);
        adm_neon_store_shifted(tmplo + j + 8,
                               adm_neon_macc4(normalize_vec_vp_lo, s_16_h, filter_lo_vec),
                               shift_vp_vec);
        adm_neon_store_shifted(
            tmphi + j, adm_neon_macc4(normalize_vec_vp_hi, s_16_l, filter_hi_vec), shift_vp_vec);
        adm_neon_store_shifted(tmphi + j + 8,
                               adm_neon_macc4(normalize_vec_vp_hi, s_16_h, filter_hi_vec),
                               shift_vp_vec);
    }

    /* Scalar tail for the columns the 16-wide vertical loop cannot reach.
     *
     * The dispatcher in integer_adm.c admits this kernel on `!(w % 8)`, but
     * the loop above advances 16 at a time and stops at `w - 15`, so for a
     * width congruent to 8 mod 16 the final 8 columns of tmplo/tmphi were
     * never written. The horizontal pass then read whatever the previous
     * row had left there, producing garbage in the last output columns —
     * silently, because every Netflix golden fixture is 1280, 1920 or 576
     * pixels wide and all three are multiples of 16. */
    for (int j = (w / 16) * 16; j < w; ++j) {
        adm_dwt2_8_neon_vpass_column(rows, j, tmplo, tmphi);
    }
}

/* One horizontal output column of all four subbands, read through the ind_x
 * mirror table exactly like the scalar adm_dwt2_8(). Used for the mirrored
 * j == 0 column and for the tail the 8-wide loop leaves over. */
static void adm_dwt2_8_neon_hpass_column(const int16_t *tmplo, const int16_t *tmphi,
                                         int *const ind_x[4], const adm_dwt_band_t *dst,
                                         int row_offset, int j)
{
    int32_t accum_a = ADM_DWT2_8_ADD_SHIFT_HP;
    int32_t accum_v = ADM_DWT2_8_ADD_SHIFT_HP;
    int32_t accum_h = ADM_DWT2_8_ADD_SHIFT_HP;
    int32_t accum_d = ADM_DWT2_8_ADD_SHIFT_HP;

    for (int tap = 0; tap < 4; tap++) {
        const int column = ind_x[tap][j];
        const int16_t s_lo = tmplo[column];
        const int16_t s_hi = tmphi[column];
        accum_a += (int32_t)dwt2_db2_coeffs_lo[tap] * s_lo;
        accum_v += (int32_t)dwt2_db2_coeffs_hi[tap] * s_lo;
        accum_h += (int32_t)dwt2_db2_coeffs_lo[tap] * s_hi;
        accum_d += (int32_t)dwt2_db2_coeffs_hi[tap] * s_hi;
    }

    dst->band_a[row_offset + j] = (int16_t)(accum_a >> ADM_DWT2_8_SHIFT_HP);
    dst->band_v[row_offset + j] = (int16_t)(accum_v >> ADM_DWT2_8_SHIFT_HP);
    dst->band_h[row_offset + j] = (int16_t)(accum_h >> ADM_DWT2_8_SHIFT_HP);
    dst->band_d[row_offset + j] = (int16_t)(accum_d >> ADM_DWT2_8_SHIFT_HP);
}

/* Horizontal pass of one output row. The 8-wide loop writes columns j..j+7
 * and reads taps up to 2 * (j + 7) + 2 without consulting ind_x, so its last
 * column must stay at or below half_w - 2 (the last column whose taps need no
 * mirror) and it must never store past half_w - 1. Column 0 and everything
 * from half_w_mod8 on go through ind_x, which applies the mirror. */
static void adm_dwt2_8_neon_hpass_row(const int16_t *tmplo, const int16_t *tmphi,
                                      int *const ind_x[4], const adm_dwt_band_t *dst,
                                      int row_offset, int half_w)
{
    const int half_w_mod8 = half_w >= 2 ? half_w - 1 - ((half_w - 2) % 8) : 1;
    const int16x4_t filter_lo_vec = vld1_s16(dwt2_db2_coeffs_lo);
    const int16x4_t filter_hi_vec = vld1_s16(dwt2_db2_coeffs_hi);
    const int32x4_t add_shift_hp_vec = vdupq_n_s32(ADM_DWT2_8_ADD_SHIFT_HP);
    const int32x4_t shift_hp_vec = vdupq_n_s32(-ADM_DWT2_8_SHIFT_HP);

    /* j = 0 is a special case: src_ind_x[k][0] is the mirrored {1, 0, 1, 2}
     * rather than {-1, 0, 1, 2}. */
    adm_dwt2_8_neon_hpass_column(tmplo, tmphi, ind_x, dst, row_offset, 0);

    /* The kernel only runs for even w (the dispatcher requires !(w % 8)), so
     * between column 1 and half_w_mod8 the taps of column j are simply
     * 2j - 1, 2j, 2j + 1 and 2j + 2 and ind_x can be ignored. */
    for (int j = 1; j < half_w_mod8; j += 8) {
        const int16_t *p_low = tmplo + ((ptrdiff_t)2 * j);
        const int16_t *p_high = tmphi + ((ptrdiff_t)2 * j);
        const int16x8x2_t low_s0s1 = vld2q_s16(p_low - 1);
        const int16x8x2_t low_s2s3 = vld2q_s16(p_low + 1);
        const int16x8x2_t high_s0s1 = vld2q_s16(p_high - 1);
        const int16x8x2_t high_s2s3 = vld2q_s16(p_high + 1);
        /* De-interleaved: val[0] of each load holds the odd-indexed samples
         * (taps 0 and 2), val[1] the even-indexed ones (taps 1 and 3). */
        const int16x8_t low_taps[4] = {low_s0s1.val[0], low_s0s1.val[1], low_s2s3.val[0],
                                       low_s2s3.val[1]};
        const int16x8_t high_taps[4] = {high_s0s1.val[0], high_s0s1.val[1], high_s2s3.val[0],
                                        high_s2s3.val[1]};
        const ptrdiff_t out = (ptrdiff_t)row_offset + j;

        adm_neon_store_shifted(dst->band_a + out,
                               adm_neon_macc4(add_shift_hp_vec, low_taps, filter_lo_vec),
                               shift_hp_vec);
        adm_neon_store_shifted(dst->band_v + out,
                               adm_neon_macc4(add_shift_hp_vec, low_taps, filter_hi_vec),
                               shift_hp_vec);
        adm_neon_store_shifted(dst->band_h + out,
                               adm_neon_macc4(add_shift_hp_vec, high_taps, filter_lo_vec),
                               shift_hp_vec);
        adm_neon_store_shifted(dst->band_d + out,
                               adm_neon_macc4(add_shift_hp_vec, high_taps, filter_hi_vec),
                               shift_hp_vec);
    }

    /* Scalar tail through ind_x: the columns the 8-wide loop must not reach,
     * including the last one, whose taps mirror back into range. Same guarded
     * bound as the x86 DWT2 kernels (Netflix/vmaf ea012e387). */
    for (int j = half_w_mod8; j < half_w; ++j) {
        adm_dwt2_8_neon_hpass_column(tmplo, tmphi, ind_x, dst, row_offset, j);
    }
}

void adm_dwt2_8_neon(const uint8_t *src, const adm_dwt_band_t *dst, AdmBuffer *buf, int w, int h,
                     int src_stride, int dst_stride)
{
    int **ind_y = buf->ind_y;
    int *const *ind_x = buf->ind_x;
    int16_t *tmplo = (int16_t *)buf->tmp_ref;
    int16_t *tmphi = tmplo + w;
    const int half_w = (w + 1) / 2;

    for (int i = 0; i < (h + 1) / 2; ++i) {
        const uint8_t *const rows[4] = {
            src + ((ptrdiff_t)ind_y[0][i] * src_stride),
            src + ((ptrdiff_t)ind_y[1][i] * src_stride),
            src + ((ptrdiff_t)ind_y[2][i] * src_stride),
            src + ((ptrdiff_t)ind_y[3][i] * src_stride),
        };
        adm_dwt2_8_neon_vpass_row(rows, w, tmplo, tmphi);
        adm_dwt2_8_neon_hpass_row(tmplo, tmphi, ind_x, dst, i * dst_stride, half_w);
    }
}

/*
 * Scale-zero decouple, four columns at a time (Netflix/vmaf 9e48141b, Dan
 * Trapp). The scalar reference is adm_decouple_cols() in
 * feature/integer_adm_kernels.h; this kernel returns its samples bit for bit.
 *
 * It takes the vector path for an integral enhancement gain limit only. The
 * scalar kernel stores MIN(rst * gain, t) as the double product truncated
 * toward zero (ADR-1413); for an integral gain that product is the int32
 * product (rst is within 2^15 and the limit within 100), so the vector lanes
 * form it in int32. A fractional limit takes the scalar kernel, which is the
 * definition of the truncated product.
 */

/* Dot product of two (h, v) pairs, rounded to float as the scalar angle test
 * rounds its int64 sums. The sums stay below 2^31, so the int64 -> double ->
 * float conversion rounds once. */
static inline float32x4_t adm_neon_dot_s16(int16x4_t ah, int16x4_t av, int16x4_t bh, int16x4_t bv)
{
    const int32x4_t h = vmull_s16(ah, bh);
    const int32x4_t v = vmull_s16(av, bv);
    const int64x2_t lo = vaddl_s32(vget_low_s32(h), vget_low_s32(v));
    const int64x2_t hi = vaddl_s32(vget_high_s32(h), vget_high_s32(v));
    return vcombine_f32(vcvt_f32_f64(vcvtq_f64_s64(lo)), vcvt_f32_f64(vcvtq_f64_s64(hi)));
}

/* The one-degree angle test of adm_angle_flag_fp64() on two lanes, in double:
 * dot >= 0 and dot^2 >= (cos^2 * |o|^2) * |t|^2, the products in that order. */
static inline uint32x2_t adm_neon_angle_f64(float32x2_t dot, float32x2_t omag, float32x2_t tmag,
                                            double cos_sq)
{
    const float64x2_t d = vmulq_n_f64(vcvt_f64_f32(dot), 1.0 / 4096.0);
    const float64x2_t o = vmulq_n_f64(vcvt_f64_f32(omag), 1.0 / 4096.0);
    const float64x2_t t = vmulq_n_f64(vcvt_f64_f32(tmag), 1.0 / 4096.0);
    const uint64x2_t non_negative = vcgeq_f64(d, vdupq_n_f64(0.0));
    const uint64x2_t inside = vcgeq_f64(vmulq_f64(d, d), vmulq_f64(vmulq_n_f64(o, cos_sq), t));
    return vmovn_u64(vandq_u64(non_negative, inside));
}

/* The angle flag of four columns: all lanes clear at gain 1, where the Q15
 * reconstruction already lies between zero and the distorted sample. */
static inline uint32x4_t adm_neon_angle4(const int16x4_t o[3], const int16x4_t t[3], double gain,
                                         double cos_sq)
{
    if (gain == 1.0) {
        return vdupq_n_u32(0);
    }
    const float32x4_t dot = adm_neon_dot_s16(o[0], o[1], t[0], t[1]);
    const float32x4_t omag = adm_neon_dot_s16(o[0], o[1], o[0], o[1]);
    const float32x4_t tmag = adm_neon_dot_s16(t[0], t[1], t[0], t[1]);
    return vcombine_u32(
        adm_neon_angle_f64(vget_low_f32(dot), vget_low_f32(omag), vget_low_f32(tmag), cos_sq),
        adm_neon_angle_f64(vget_high_f32(dot), vget_high_f32(omag), vget_high_f32(tmag), cos_sq));
}

/* adm_decouple_band() on four columns of one band. `ref` is the reference
 * band at the first column: its samples index the reciprocal table. */
static inline int16x4_t adm_neon_decouple_band(const int16_t *ref, int16x4_t o, int16x4_t t,
                                               uint32x4_t angle, int gain, const int32_t *lookup)
{
    const int32_t div[4] = {lookup[ref[0] + 32768], lookup[ref[1] + 32768], lookup[ref[2] + 32768],
                            lookup[ref[3] + 32768]};
    const int32x4_t recip = vld1q_s32(div);
    const int32x4_t dis = vmovl_s16(t);
    const int32x4_t orig = vmovl_s16(o);
    const int32x4_t zero = vdupq_n_s32(0);
    const int32x4_t ratio =
        vcombine_s32(vrshrn_n_s64(vmull_s32(vget_low_s32(recip), vget_low_s32(dis)), 15),
                     vrshrn_n_s64(vmull_s32(vget_high_s32(recip), vget_high_s32(dis)), 15));
    const int32x4_t k = vbslq_s32(vceqq_s32(orig, zero), vdupq_n_s32(32768),
                                  vmaxq_s32(zero, vminq_s32(ratio, vdupq_n_s32(32768))));
    const int32x4_t rst = vrshrq_n_s32(vmulq_s32(k, orig), 15);
    if (!vmaxvq_u32(angle)) {
        return vmovn_s32(rst);
    }
    const int32x4_t scaled = vmulq_n_s32(rst, gain);
    const uint32x4_t active = vandq_u32(angle, vcgtq_s32(k, zero));
    const int32x4_t negative =
        vbslq_s32(vandq_u32(active, vcltq_s32(orig, zero)), vmaxq_s32(scaled, dis), rst);
    return vmovn_s32(
        vbslq_s32(vandq_u32(active, vcgtq_s32(orig, zero)), vminq_s32(scaled, dis), negative));
}

/* Decouple the four columns starting at `off` and store both outputs. */
static inline void adm_neon_decouple4(AdmBuffer *buf, int off, int gain, double cos_sq,
                                      const int32_t *lookup)
{
    const int16_t *const ref[3] = {buf->ref_dwt2.band_h + off, buf->ref_dwt2.band_v + off,
                                   buf->ref_dwt2.band_d + off};
    const int16_t *const dis[3] = {buf->dis_dwt2.band_h + off, buf->dis_dwt2.band_v + off,
                                   buf->dis_dwt2.band_d + off};
    int16_t *const rst[3] = {buf->decouple_r.band_h + off, buf->decouple_r.band_v + off,
                             buf->decouple_r.band_d + off};
    int16_t *const add[3] = {buf->decouple_a.band_h + off, buf->decouple_a.band_v + off,
                             buf->decouple_a.band_d + off};
    int16x4_t o[3];
    int16x4_t t[3];

    for (int b = 0; b < 3; ++b) {
        o[b] = vld1_s16(ref[b]);
        t[b] = vld1_s16(dis[b]);
    }
    const uint32x4_t angle = adm_neon_angle4(o, t, (double)gain, cos_sq);
    for (int b = 0; b < 3; ++b) {
        const int16x4_t r = adm_neon_decouple_band(ref[b], o[b], t[b], angle, gain, lookup);
        vst1_s16(rst[b], r);
        vst1_s16(add[b], vsub_s16(t[b], r));
    }
}

void adm_decouple_neon(AdmBuffer *buf, int w, int h, int stride, double adm_enhn_gain_limit,
                       int32_t *adm_div_lookup)
{
    const float cos_1deg_sq = adm_cos_1deg_sq();
    const AdmBorder b = adm_border_filt(w, h);
    const int width = b.right - b.left;
    const int gain = (int)adm_enhn_gain_limit;

    if (width < 4 || adm_enhn_gain_limit != (double)gain) {
        for (int i = b.top; i < b.bottom; ++i) {
            adm_decouple_cols(buf, i, stride, b.left, b.right, adm_enhn_gain_limit, adm_div_lookup,
                              cos_1deg_sq);
        }
        return;
    }
    for (int i = b.top; i < b.bottom; ++i) {
        /* Every group of four starts inside the region; the last one is moved
         * back to end on the last column, which recomputes up to three columns
         * from unchanged inputs. */
        for (int j = b.left; j < b.right; j += 4) {
            const int j0 = (j > b.right - 4) ? b.right - 4 : j;
            adm_neon_decouple4(buf, (i * stride) + j0, gain, (double)cos_1deg_sq, adm_div_lookup);
        }
    }
}
