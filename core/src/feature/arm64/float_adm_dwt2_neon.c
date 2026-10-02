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

/*
 * Non-contracting NEON DWT2 kernel for float-ADM.
 *
 * The production scalar `adm_dwt2_s` carries a function-scoped contraction
 * guard because its results feed immutable Netflix goldens.  The NEON body
 * mirrors that stable contract with explicit `vmulq_laneq_f32` followed by
 * `vaddq_f32`; its scalar tail and horizontal pass use the same left-to-right
 * multiply/add sequence.  The TU-level guard prevents implicit fusion.
 *
 * See ADR-1057's 2026-08-31 update.
 */

/* Belt-and-suspenders: suppress compiler FP contraction at the TU level.
 * Clang honours `#pragma clang fp contract(off)`.  GCC does not implement
 * that pragma but does honour the function attribute below; apply it to
 * every function in this file via the __attribute__ on the definition.
 * The meson.build `-ffp-contract=off` flag is the primary guard; the
 * pragma/attribute are fallbacks for environments that call into this
 * object without the meson carve-out. */
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

#include <arm_neon.h>
#include <stddef.h>
#include "float_adm_neon.h"
#include "mem.h"

static const float dwt2_db2_coeffs_lo_s[4] = {0.482962913144690f, 0.836516303737469f,
                                              0.224143868041857f, -0.129409522550921f};

static const float dwt2_db2_coeffs_hi_s[4] = {-0.129409522550921f, -0.224143868041857f,
                                              0.836516303737469f, -0.482962913144690f};

/* GCC does not honour `#pragma clang fp contract(off)`, so every function
 * below also carries a per-function attribute as a belt-and-suspenders guard,
 * as `adm_dwt2_s` and its two pass helpers do in adm_tools.c. */

/* Four columns of one filter: `f` holds the four taps, `s0..s3` the four
 * source rows.  Start at +0 exactly like adm_dwt2_s: the first add is
 * load-bearing for signed-zero parity.  Each tap then uses an explicit
 * multiply followed by an add. */
#if defined(__GNUC__) && !defined(__clang__)
__attribute__((optimize("-ffp-contract=off")))
#endif
static inline float32x4_t dwt2_vertical_4_neon(float32x4_t f, float32x4_t s0, float32x4_t s1,
                                               float32x4_t s2, float32x4_t s3)
{
    float32x4_t acc = vdupq_n_f32(0.0f);
    acc = vaddq_f32(acc, vmulq_laneq_f32(s0, f, 0));
    acc = vaddq_f32(acc, vmulq_laneq_f32(s1, f, 1));
    acc = vaddq_f32(acc, vmulq_laneq_f32(s2, f, 2));
    acc = vaddq_f32(acc, vmulq_laneq_f32(s3, f, 3));
    return acc;
}

/* Vertical pass of one output row: the four source rows `row[0..3]` filtered
 * into tmplo and tmphi, `w` samples each. */
#if defined(__GNUC__) && !defined(__clang__)
__attribute__((optimize("-ffp-contract=off")))
#endif
static void dwt2_vertical_row_neon(float32x4_t flo, float32x4_t fhi, const float *const row[4],
                                   int w, float *tmplo, float *tmphi)
{
    const float *filter_lo = dwt2_db2_coeffs_lo_s;
    const float *filter_hi = dwt2_db2_coeffs_hi_s;

    /* Process 4 columns at a time with NEON. */
    int j = 0;
    for (; j + 3 < w; j += 4) {
        float32x4_t s0 = vld1q_f32(row[0] + j);
        float32x4_t s1 = vld1q_f32(row[1] + j);
        float32x4_t s2 = vld1q_f32(row[2] + j);
        float32x4_t s3 = vld1q_f32(row[3] + j);

        vst1q_f32(tmplo + j, dwt2_vertical_4_neon(flo, s0, s1, s2, s3));
        vst1q_f32(tmphi + j, dwt2_vertical_4_neon(fhi, s0, s1, s2, s3));
    }

    /* Scalar tail for remaining columns. */
    for (; j < w; ++j) {
        float s0 = row[0][j];
        float s1 = row[1][j];
        float s2 = row[2][j];
        float s3 = row[3][j];

        float accum = 0.0f;
        accum += filter_lo[0] * s0;
        accum += filter_lo[1] * s1;
        accum += filter_lo[2] * s2;
        accum += filter_lo[3] * s3;
        tmplo[j] = accum;

        accum = 0.0f;
        accum += filter_hi[0] * s0;
        accum += filter_hi[1] * s1;
        accum += filter_hi[2] * s2;
        accum += filter_hi[3] * s3;
        tmphi[j] = accum;
    }
}

/* Horizontal pass (lo and hi) of one output row: scalar due to indirect
 * indexing.  `a`, `v`, `h` and `d` are that row of the four bands. */
#if defined(__GNUC__) && !defined(__clang__)
__attribute__((optimize("-ffp-contract=off")))
#endif
static void dwt2_horizontal_row_neon(const float *tmplo, const float *tmphi, int **ind_x,
                                     int half_w, float *a, float *v, float *h, float *d)
{
    const float *filter_lo = dwt2_db2_coeffs_lo_s;
    const float *filter_hi = dwt2_db2_coeffs_hi_s;

    for (int j = 0; j < half_w; ++j) {
        int j0 = ind_x[0][j];
        int j1 = ind_x[1][j];
        int j2 = ind_x[2][j];
        int j3 = ind_x[3][j];

        float sl0 = tmplo[j0];
        float sl1 = tmplo[j1];
        float sl2 = tmplo[j2];
        float sl3 = tmplo[j3];

        float accum = 0.0f;
        accum += filter_lo[0] * sl0;
        accum += filter_lo[1] * sl1;
        accum += filter_lo[2] * sl2;
        accum += filter_lo[3] * sl3;
        a[j] = accum;

        accum = 0.0f;
        accum += filter_hi[0] * sl0;
        accum += filter_hi[1] * sl1;
        accum += filter_hi[2] * sl2;
        accum += filter_hi[3] * sl3;
        v[j] = accum;

        float sh0 = tmphi[j0];
        float sh1 = tmphi[j1];
        float sh2 = tmphi[j2];
        float sh3 = tmphi[j3];

        accum = 0.0f;
        accum += filter_lo[0] * sh0;
        accum += filter_lo[1] * sh1;
        accum += filter_lo[2] * sh2;
        accum += filter_lo[3] * sh3;
        h[j] = accum;

        accum = 0.0f;
        accum += filter_hi[0] * sh0;
        accum += filter_hi[1] * sh1;
        accum += filter_hi[2] * sh2;
        accum += filter_hi[3] * sh3;
        d[j] = accum;
    }
}

#if defined(__GNUC__) && !defined(__clang__)
__attribute__((optimize("-ffp-contract=off")))
#endif
void float_adm_dwt2_neon(const float *src, const adm_dwt_band_t_s *dst, int **ind_y, int **ind_x,
                         int w, int h, int src_stride, int dst_stride)
{
    int src_px_stride = src_stride / sizeof(float);
    int dst_px_stride = dst_stride / sizeof(float);

    /* aligned_malloc may return NULL on OOM; the scalar reference
     * (adm_tools.c::adm_dwt2_s) treats this as -ENOMEM and aborts the
     * frame.  This function signature is `void` (matched against the
     * AVX-512 / AVX2 siblings — wiring tracked in adm.c ADR-0873
     * follow-up), so we mirror the AVX-512 sibling's behaviour:
     * release whichever allocation succeeded and silently no-op the
     * transform.  Without this guard the NEON stores at the vertical
     * pass NULL-deref and segfault. */
    float *tmplo = aligned_malloc(ALIGN_CEIL(sizeof(float) * w), MAX_ALIGN);
    float *tmphi = aligned_malloc(ALIGN_CEIL(sizeof(float) * w), MAX_ALIGN);
    if (!tmplo || !tmphi) {
        aligned_free(tmplo);
        aligned_free(tmphi);
        return;
    }

    /* Load filter coefficients into NEON registers. */
    const float32x4_t flo = vld1q_f32(dwt2_db2_coeffs_lo_s);
    const float32x4_t fhi = vld1q_f32(dwt2_db2_coeffs_hi_s);

    for (int i = 0; i < (h + 1) / 2; ++i) {
        const float *const row[4] = {
            src + (ptrdiff_t)ind_y[0][i] * src_px_stride,
            src + (ptrdiff_t)ind_y[1][i] * src_px_stride,
            src + (ptrdiff_t)ind_y[2][i] * src_px_stride,
            src + (ptrdiff_t)ind_y[3][i] * src_px_stride,
        };
        const ptrdiff_t out = (ptrdiff_t)i * dst_px_stride;

        dwt2_vertical_row_neon(flo, fhi, row, w, tmplo, tmphi);
        dwt2_horizontal_row_neon(tmplo, tmphi, ind_x, (w + 1) / 2, dst->band_a + out,
                                 dst->band_v + out, dst->band_h + out, dst->band_d + out);
    }

    aligned_free(tmplo);
    aligned_free(tmphi);
}
