/**
 *
 *  Copyright 2016-2026 Netflix, Inc.
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

#include <immintrin.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

#include "feature/adm_tools.h"
#include "mem.h"
#include "float_adm_avx512.h"

static inline __m512 avx512_abs_ps(__m512 v)
{
    const __m512i mask = _mm512_set1_epi32(0x7FFFFFFF);
    return _mm512_castsi512_ps(_mm512_and_si512(_mm512_castps_si512(v), mask));
}

/*
 * Horizontally sum 4 doubles in a __m256d to a scalar double.
 * Tree order: (v[0]+v[1]) + (v[2]+v[3]).
 * Mirrors hadd_pd4 in float_adm_avx2.c — kept here to avoid a cross-TU
 * dependency (each TU is compiled with its own ISA flags).
 */
/* ADR-0139 bit-exactness: the horizontal double reduction stays inline; it
 * must not be outline-called. */
static inline double hadd_pd4(__m256d v)
{
    __m256d h = _mm256_hadd_pd(v, v);
    return _mm_cvtsd_f64(_mm256_castpd256_pd128(h)) + _mm_cvtsd_f64(_mm256_extractf128_pd(h, 1));
}

/*
 * Widen all 16 float lanes of a __m512 to double and horizontally sum.
 * Splits into four 4-float groups via _mm512_extractf32x4_ps, widens each
 * with _mm256_cvtps_pd, and reduces with hadd_pd4.  The four group-sums are
 * then added left-to-right.  This avoids a float-precision intermediate store
 * and keeps the accumulation in double throughout — ADR-0139 / F2 fix.
 */
static inline double hsum_ps_to_double(__m512 v)
{
    __m256d d0 = _mm256_cvtps_pd(_mm512_extractf32x4_ps(v, 0));
    __m256d d1 = _mm256_cvtps_pd(_mm512_extractf32x4_ps(v, 1));
    __m256d d2 = _mm256_cvtps_pd(_mm512_extractf32x4_ps(v, 2));
    __m256d d3 = _mm256_cvtps_pd(_mm512_extractf32x4_ps(v, 3));
    return hadd_pd4(d0) + hadd_pd4(d1) + hadd_pd4(d2) + hadd_pd4(d3);
}

static const float dwt2_filter_lo[4] = {0.482962913144690f, 0.836516303737469f, 0.224143868041857f,
                                        -0.129409522550921f};
static const float dwt2_filter_hi[4] = {-0.129409522550921f, -0.224143868041857f,
                                        0.836516303737469f, -0.482962913144690f};

/* The broadcast taps of both passes. */
typedef struct Dwt2TapsAvx512 {
    __m512 lo[4];
    __m512 hi[4];
} Dwt2TapsAvx512;

/* One row of the four output bands. */
typedef struct Dwt2BandRow {
    float *a;
    float *v;
    float *h;
    float *d;
} Dwt2BandRow;

/* Vertical pass of one output row: the four source rows `row[0..3]` filtered
 * into tmplo and tmphi, `w` samples each. */
static void dwt2_vertical_row_avx512(const Dwt2TapsAvx512 *taps, const float *const row[4], int w,
                                     float *tmplo, float *tmphi)
{
    const float *filter_lo = dwt2_filter_lo;
    const float *filter_hi = dwt2_filter_hi;
    int j;

    /* Vertical pass: process 16 floats per iteration with AVX-512.
     * F3: mul+add chains match scalar tail; -ffp-contract=off per-TU
     * (meson.build carve-out) prevents auto-FMA in both paths. */
    for (j = 0; j + 16 <= w; j += 16) {
        __m512 s0 = _mm512_loadu_ps(row[0] + j);
        __m512 s1 = _mm512_loadu_ps(row[1] + j);
        __m512 s2 = _mm512_loadu_ps(row[2] + j);
        __m512 s3 = _mm512_loadu_ps(row[3] + j);

        __m512 lo_acc = _mm512_mul_ps(taps->lo[0], s0);
        lo_acc = _mm512_add_ps(lo_acc, _mm512_mul_ps(taps->lo[1], s1));
        lo_acc = _mm512_add_ps(lo_acc, _mm512_mul_ps(taps->lo[2], s2));
        lo_acc = _mm512_add_ps(lo_acc, _mm512_mul_ps(taps->lo[3], s3));
        _mm512_storeu_ps(tmplo + j, lo_acc);

        __m512 hi_acc = _mm512_mul_ps(taps->hi[0], s0);
        hi_acc = _mm512_add_ps(hi_acc, _mm512_mul_ps(taps->hi[1], s1));
        hi_acc = _mm512_add_ps(hi_acc, _mm512_mul_ps(taps->hi[2], s2));
        hi_acc = _mm512_add_ps(hi_acc, _mm512_mul_ps(taps->hi[3], s3));
        _mm512_storeu_ps(tmphi + j, hi_acc);
    }

    /* Scalar tail for vertical pass */
    for (; j < w; ++j) {
        float s0 = row[0][j];
        float s1 = row[1][j];
        float s2 = row[2][j];
        float s3 = row[3][j];

        tmplo[j] = filter_lo[0] * s0 + filter_lo[1] * s1 + filter_lo[2] * s2 + filter_lo[3] * s3;
        tmphi[j] = filter_hi[0] * s0 + filter_hi[1] * s1 + filter_hi[2] * s2 + filter_hi[3] * s3;
    }
}

/* Horizontal pass, scalar, for output column j: the left boundary (j = 0,
 * reflection) and the right tail, through ind_x. */
static void dwt2_horizontal_scalar(const float *tmplo, const float *tmphi, int **ind_x, int j,
                                   const Dwt2BandRow *out)
{
    const float *filter_lo = dwt2_filter_lo;
    const float *filter_hi = dwt2_filter_hi;
    int j0 = ind_x[0][j];
    int j1 = ind_x[1][j];
    int j2 = ind_x[2][j];
    int j3 = ind_x[3][j];
    float sl0 = tmplo[j0];
    float sl1 = tmplo[j1];
    float sl2 = tmplo[j2];
    float sl3 = tmplo[j3];

    out->a[j] = filter_lo[0] * sl0 + filter_lo[1] * sl1 + filter_lo[2] * sl2 + filter_lo[3] * sl3;
    out->v[j] = filter_hi[0] * sl0 + filter_hi[1] * sl1 + filter_hi[2] * sl2 + filter_hi[3] * sl3;

    float sh0 = tmphi[j0];
    float sh1 = tmphi[j1];
    float sh2 = tmphi[j2];
    float sh3 = tmphi[j3];

    out->h[j] = filter_lo[0] * sh0 + filter_lo[1] * sh1 + filter_lo[2] * sh2 + filter_lo[3] * sh3;
    out->d[j] = filter_hi[0] * sh0 + filter_hi[1] * sh1 + filter_hi[2] * sh2 + filter_hi[3] * sh3;
}

/* Sixteen outputs of the low and of the high horizontal filter from one
 * temporary row. `taps_at` points at tmp[2j]: the four taps of output j + k
 * are tmp[2(j+k) - 1 .. 2(j+k) + 2], deinterleaved from two pairs of loads. */
static inline void dwt2_horizontal_16_avx512(const Dwt2TapsAvx512 *taps, const float *taps_at,
                                             float *out_lo, float *out_hi)
{
    /* Permutation indices for even/odd deinterleaving across 2 zmm regs */
    const __m512i idx_even =
        _mm512_set_epi32(30, 28, 26, 24, 22, 20, 18, 16, 14, 12, 10, 8, 6, 4, 2, 0);
    const __m512i idx_odd =
        _mm512_set_epi32(31, 29, 27, 25, 23, 21, 19, 17, 15, 13, 11, 9, 7, 5, 3, 1);

    __m512 A = _mm512_loadu_ps(taps_at - 1);
    __m512 B = _mm512_loadu_ps(taps_at - 1 + 16);
    __m512 tap0 = _mm512_permutex2var_ps(A, idx_even, B);
    __m512 tap1 = _mm512_permutex2var_ps(A, idx_odd, B);

    __m512 C = _mm512_loadu_ps(taps_at + 1);
    __m512 D = _mm512_loadu_ps(taps_at + 1 + 16);
    __m512 tap2 = _mm512_permutex2var_ps(C, idx_even, D);
    __m512 tap3 = _mm512_permutex2var_ps(C, idx_odd, D);

    __m512 lo = _mm512_mul_ps(taps->lo[0], tap0);
    lo = _mm512_add_ps(lo, _mm512_mul_ps(taps->lo[1], tap1));
    lo = _mm512_add_ps(lo, _mm512_mul_ps(taps->lo[2], tap2));
    lo = _mm512_add_ps(lo, _mm512_mul_ps(taps->lo[3], tap3));
    _mm512_storeu_ps(out_lo, lo);

    __m512 hi = _mm512_mul_ps(taps->hi[0], tap0);
    hi = _mm512_add_ps(hi, _mm512_mul_ps(taps->hi[1], tap1));
    hi = _mm512_add_ps(hi, _mm512_mul_ps(taps->hi[2], tap2));
    hi = _mm512_add_ps(hi, _mm512_mul_ps(taps->hi[3], tap3));
    _mm512_storeu_ps(out_hi, hi);
}

/* Horizontal pass of one output row: vectorized interior with deinterleave,
 * scalar for left boundary (j=0 reflection) and right tail. */
static void dwt2_horizontal_row_avx512(const Dwt2TapsAvx512 *taps, const float *tmplo,
                                       const float *tmphi, int **ind_x, int w,
                                       const Dwt2BandRow *out)
{
    const int half_w = (w + 1) / 2;

    /* Scalar: j=0 (left boundary has reflection) */
    dwt2_horizontal_scalar(tmplo, tmphi, ind_x, 0, out);

    /* AVX-512 vectorized interior: 16 outputs per iteration.
     * For j>=1, tap indices are regular stride-2:
     *   tap0 = tmplo[2j-1], tap1 = tmplo[2j], tap2 = tmplo[2j+1], tap3 = tmplo[2j+2]
     * Deinterleave even/odd from 32-element load pairs. */
    int j = 1;
    for (; j + 16 <= half_w && 2 * j + 32 < w; j += 16) {
        const ptrdiff_t at = (ptrdiff_t)2 * j;
        dwt2_horizontal_16_avx512(taps, tmplo + at, out->a + j, out->v + j);
        dwt2_horizontal_16_avx512(taps, tmphi + at, out->h + j, out->d + j);
    }

    /* Scalar tail: right boundary + remaining positions */
    for (; j < half_w; ++j) {
        dwt2_horizontal_scalar(tmplo, tmphi, ind_x, j, out);
    }
}

void float_adm_dwt2_avx512(const float *src, const adm_dwt_band_t_s *dst, int **ind_y, int **ind_x,
                           int w, int h, int src_stride, int dst_stride)
{
    const float *filter_lo = dwt2_filter_lo;
    const float *filter_hi = dwt2_filter_hi;

    int src_px_stride = src_stride / sizeof(float);
    int dst_px_stride = dst_stride / sizeof(float);

    float *tmplo = aligned_malloc(ALIGN_CEIL(sizeof(float) * (w + 32)), MAX_ALIGN);
    float *tmphi = aligned_malloc(ALIGN_CEIL(sizeof(float) * (w + 32)), MAX_ALIGN);
    if (!tmplo || !tmphi) {
        aligned_free(tmplo);
        aligned_free(tmphi);
        return;
    }
    // Zero guard zone to avoid garbage reads at right boundary
    memset(tmplo + w, 0, 32 * sizeof(float));
    memset(tmphi + w, 0, 32 * sizeof(float));

    const Dwt2TapsAvx512 taps = {
        .lo = {_mm512_set1_ps(filter_lo[0]), _mm512_set1_ps(filter_lo[1]),
               _mm512_set1_ps(filter_lo[2]), _mm512_set1_ps(filter_lo[3])},
        .hi = {_mm512_set1_ps(filter_hi[0]), _mm512_set1_ps(filter_hi[1]),
               _mm512_set1_ps(filter_hi[2]), _mm512_set1_ps(filter_hi[3])},
    };

    for (int i = 0; i < (h + 1) / 2; ++i) {
        const float *const row[4] = {
            src + (ptrdiff_t)ind_y[0][i] * src_px_stride,
            src + (ptrdiff_t)ind_y[1][i] * src_px_stride,
            src + (ptrdiff_t)ind_y[2][i] * src_px_stride,
            src + (ptrdiff_t)ind_y[3][i] * src_px_stride,
        };
        const ptrdiff_t at = (ptrdiff_t)i * dst_px_stride;
        const Dwt2BandRow out = {dst->band_a + at, dst->band_v + at, dst->band_h + at,
                                 dst->band_d + at};

        dwt2_vertical_row_avx512(&taps, row, w, tmplo, tmphi);
        dwt2_horizontal_row_avx512(&taps, tmplo, tmphi, ind_x, w, &out);
    }

    aligned_free(tmplo);
    aligned_free(tmphi);
}

void float_adm_csf_avx512(const float *src, float *dst, float *flt, int w, int h, int src_stride,
                          int dst_stride, float factor, float one_by_30)
{
    int src_px_stride = src_stride / sizeof(float);
    int dst_px_stride = dst_stride / sizeof(float);

    __m512 vfactor = _mm512_set1_ps(factor);
    __m512 vone_by_30 = _mm512_set1_ps(one_by_30);

    int i;
    int j;

    for (i = 0; i < h; ++i) {
        const float *src_row = src + (ptrdiff_t)i * src_px_stride;
        float *dst_row = dst + (ptrdiff_t)i * dst_px_stride;
        float *flt_row = flt + (ptrdiff_t)i * dst_px_stride;

        for (j = 0; j + 16 <= w; j += 16) {
            __m512 sv = _mm512_loadu_ps(src_row + j);
            __m512 dst_val = _mm512_mul_ps(vfactor, sv);
            _mm512_storeu_ps(dst_row + j, dst_val);
            __m512 abs_dst = avx512_abs_ps(dst_val);
            __m512 flt_val = _mm512_mul_ps(vone_by_30, abs_dst);
            _mm512_storeu_ps(flt_row + j, flt_val);
        }

        /* Scalar tail */
        for (; j < w; ++j) {
            float dst_val = factor * src_row[j];
            dst_row[j] = dst_val;
            flt_row[j] = one_by_30 * fabsf(dst_val);
        }
    }
}

float float_adm_csf_den_scale_avx512(const float *src, int w, int h, int src_stride, int left,
                                     int top, int right, int bottom, float factor)
{
    (void)w;
    (void)h;
    int src_px_stride = src_stride / sizeof(float);

    __m512 vfactor = _mm512_set1_ps(factor);

    double accum = 0.0;
    int i;
    int j;

    for (i = top; i < bottom; ++i) {
        const float *row = src + (ptrdiff_t)i * src_px_stride;
        double row_accum = 0.0;

        for (j = left; j + 16 <= right; j += 16) {
            __m512 sv = _mm512_loadu_ps(row + j);
            __m512 val = avx512_abs_ps(_mm512_mul_ps(vfactor, sv));
            __m512 val2 = _mm512_mul_ps(val, val);
            __m512 val3 = _mm512_mul_ps(val2, val);

            /*
             * F2 fix: widen 16 float lanes to double via _mm512_extractf32x4_ps
             * + _mm256_cvtps_pd (4 groups of 4), reducing each group with
             * hadd_pd4 in double.  Avoids a float-precision intermediate store.
             * See hsum_ps_to_double() above — ADR-0139.
             */
            row_accum += hsum_ps_to_double(val3);
        }

        for (; j < right; ++j) {
            float val = fabsf(factor * row[j]);
            row_accum += (double)(val * val * val);
        }

        accum += row_accum;
    }

    return (float)accum;
}

float float_adm_sum_cube_avx512(const float *x, int w, int h, int stride, int left, int top,
                                int right, int bottom)
{
    (void)w;
    (void)h;
    int px_stride = stride / sizeof(float);

    double accum = 0.0;
    int i;
    int j;

    for (i = top; i < bottom; ++i) {
        const float *row = x + (ptrdiff_t)i * px_stride;
        double row_accum = 0.0;

        for (j = left; j + 16 <= right; j += 16) {
            __m512 val = avx512_abs_ps(_mm512_loadu_ps(row + j));
            __m512 val2 = _mm512_mul_ps(val, val);
            __m512 val3 = _mm512_mul_ps(val2, val);

            /* F2 fix: same hsum_ps_to_double path as csf_den_scale above. */
            row_accum += hsum_ps_to_double(val3);
        }

        for (; j < right; ++j) {
            float val = fabsf(row[j]);
            row_accum += (double)(val * val * val);
        }

        accum += row_accum;
    }

    return (float)accum;
}
