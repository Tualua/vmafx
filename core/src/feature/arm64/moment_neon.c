/**
 *  Copyright 2016-2026 Netflix, Inc.
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 *  NEON implementations of compute_1st_moment / compute_2nd_moment for
 *  the float_moment feature extractor (T7-19, ADR-0179).
 *
 *  Bit-exactness contract (ADR-1500): the scalar reference (moment.c) adds
 *  every sample, or its float square, into one double in raster order. Once
 *  that double passes 2^53 units of its smallest term, every add rounds, so
 *  any other grouping of the adds (lane accumulators, per-row partial sums)
 *  returns another number. These kernels square four samples in one float
 *  vector, as the scalar squares each sample in float, and add the four
 *  values into the running double one after the other, in raster order, as
 *  moment_avx2.c does. They return the scalar's bits on every input.
 */
#include <arm_neon.h>
#include <assert.h>
#include <stddef.h>

#include "moment_neon.h"

/* `cum` plus the four lanes of `v`, added one after the other in lane order:
 * the scalar's adds of four consecutive samples. */
static inline double moment_add4(double cum, float32x4_t v)
{
    float lanes[4];
    vst1q_f32(lanes, v);
    for (int k = 0; k < 4; ++k)
        cum += (double)lanes[k];
    return cum;
}

int compute_1st_moment_neon(const float *pic, int w, int h, int stride, double *score)
{
    assert(pic != NULL);
    assert(score != NULL);
    assert(w > 0);
    assert(h > 0);

    const int stride_f = stride / (int)sizeof(float);
    double cum = 0.0;

    for (int i = 0; i < h; ++i) {
        const float *row = pic + (size_t)i * (size_t)stride_f;
        int j = 0;

        for (; j + 4 <= w; j += 4)
            cum = moment_add4(cum, vld1q_f32(row + j));
        for (; j < w; ++j)
            cum += (double)row[j];
    }

    cum /= (double)w * (double)h;
    *score = cum;
    return 0;
}

int compute_2nd_moment_neon(const float *pic, int w, int h, int stride, double *score)
{
    assert(pic != NULL);
    assert(score != NULL);
    assert(w > 0);
    assert(h > 0);

    const int stride_f = stride / (int)sizeof(float);
    double cum = 0.0;

    for (int i = 0; i < h; ++i) {
        const float *row = pic + (size_t)i * (size_t)stride_f;
        int j = 0;

        for (; j + 4 <= w; j += 4) {
            const float32x4_t v = vld1q_f32(row + j);
            cum = moment_add4(cum, vmulq_f32(v, v));
        }
        for (; j < w; ++j) {
            /* Square in float (not double), as the scalar reference
             * (moment.c:compute_2nd_moment, `pic_ * pic_`) and the vector
             * loop (`vmulq_f32(v, v)`) do. A double square rounds
             * differently. */
            const float p = row[j];
            const float term = p * p;
            cum += (double)term;
        }
    }

    cum /= (double)w * (double)h;
    *score = cum;
    return 0;
}
