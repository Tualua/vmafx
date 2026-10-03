/**
 *  Copyright 2016-2026 Netflix, Inc.
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 *  aarch64 SVE2 port of compute_1st_moment / compute_2nd_moment for the
 *  float_moment feature extractor (ADR-0584).
 *
 *  Bit-exactness contract (ADR-1500): the scalar reference (moment.c) adds
 *  every sample, or its float square, into one double in raster order. Once
 *  that double passes 2^53 units of its smallest term, every add rounds, so
 *  any other grouping of the adds returns another number, and a grouping by
 *  vector lanes would also depend on the vector length. These kernels load
 *  (and for the second moment square, in float, as the scalar does) one
 *  vector of samples at a time, store the active lanes, and add them into the
 *  running double one after the other, in raster order, as moment_avx2.c
 *  does. The result is the scalar's bits on every input and every vector
 *  length; no lane is ever widened or reduced inside a vector.
 *
 *  The predicate `svwhilelt_b32(j, w)` is active on the first min(VL, w - j)
 *  lanes, in order, so the first `n` stored lanes are row[j .. j + n - 1].
 *  The largest vector SVE allows is 2048 bits, 64 f32 lanes, which bounds the
 *  lane buffer.
 *
 *  Darwin opt-out: ADR-0419.  The runtime gate in arm/cpu.c is
 *  `__linux__`-gated so VMAF_ARM_CPU_FLAG_SVE2 is never set on Apple Silicon
 *  regardless of chip capability.  The meson build gate mirrors
 *  `is_sve2_supported` which is forced false on Darwin (ADR-0419).
 */

#include <arm_sve.h>
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "moment_sve2.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#endif
#pragma STDC FP_CONTRACT OFF
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

/* f32 lanes of the widest SVE vector (2048 bits). */
#define MOMENT_SVE2_MAX_LANES 64

/* `cum` plus the active lanes of `v` under `pg` (a `svwhilelt_b32` predicate,
 * so the active lanes are the first ones), added one after the other in lane
 * order. `lanes` holds MOMENT_SVE2_MAX_LANES floats. */
static inline double moment_add_active(double cum, svbool_t pg, svfloat32_t v, float *lanes)
{
    svst1_f32(pg, lanes, v);
    const int n = (int)svcntp_b32(svptrue_b32(), pg);
    for (int k = 0; k < n; ++k)
        cum += (double)lanes[k];
    return cum;
}

int compute_1st_moment_sve2(const float *pic, int w, int h, int stride, double *score)
{
    assert(pic != NULL);
    assert(score != NULL);
    assert(w > 0);
    assert(h > 0);
    assert(svcntw() <= MOMENT_SVE2_MAX_LANES);

    const int stride_f = stride / (int)sizeof(float);
    const int step = (int)svcntw();
    float lanes[MOMENT_SVE2_MAX_LANES] = {0.0f};
    double cum = 0.0;

    for (int i = 0; i < h; ++i) {
        const float *row = pic + (size_t)i * (size_t)stride_f;
        for (int j = 0; j < w; j += step) {
            const svbool_t pg = svwhilelt_b32((uint32_t)j, (uint32_t)w);
            cum = moment_add_active(cum, pg, svld1_f32(pg, row + j), lanes);
        }
    }

    cum /= (double)w * (double)h;
    *score = cum;
    return 0;
}

int compute_2nd_moment_sve2(const float *pic, int w, int h, int stride, double *score)
{
    assert(pic != NULL);
    assert(score != NULL);
    assert(w > 0);
    assert(h > 0);
    assert(svcntw() <= MOMENT_SVE2_MAX_LANES);

    const int stride_f = stride / (int)sizeof(float);
    const int step = (int)svcntw();
    float lanes[MOMENT_SVE2_MAX_LANES] = {0.0f};
    double cum = 0.0;

    for (int i = 0; i < h; ++i) {
        const float *row = pic + (size_t)i * (size_t)stride_f;
        for (int j = 0; j < w; j += step) {
            const svbool_t pg = svwhilelt_b32((uint32_t)j, (uint32_t)w);
            const svfloat32_t v = svld1_f32(pg, row + j);
            /* Square in f32, as the scalar reference does (moment.c:
             * `pic_ * pic_`). */
            cum = moment_add_active(cum, pg, svmul_f32_x(pg, v, v), lanes);
        }
    }

    cum /= (double)w * (double)h;
    *score = cum;
    return 0;
}
