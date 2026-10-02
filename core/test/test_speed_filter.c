/**
 * Copyright 2026 Dan Trapp.
 * Copyright 2026 Lusoris
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * Licensed under the BSD+Patent License (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSDplusPatent
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * vif_filter1d_dec16_s() against vif_filter1d_s() followed by vif_dec16_s()
 * (Netflix/vmaf 76ea5f03). SpEED's anti-alias step keeps one filtered sample
 * in 256; on every target but x86 the extractor computes only those, and the
 * result has to be the bits the two calls leave there.
 *
 *   test_filter_dec16          the upstream matrix: sizes that are and are
 *                              not multiples of 16, tight and padded strides,
 *                              an unaligned source, five image patterns,
 *                              every filter width from 3 to 37 taps. The
 *                              sizes of upstream's checkasm case (cea2b4d8;
 *                              the fork carries no checkasm tree) and the
 *                              planes SpEED meets here (80x80, the smallest
 *                              it accepts, odd planes, the Netflix chroma
 *                              plane) are added.
 *   test_filter_and_downscale  speed_internal_filter_and_downscale(), the
 *                              mirror of speed.c's static
 *                              filter_and_downscale(), against the two calls
 *                              spelled out: the whole frame buffer, so the
 *                              copy back from the scratch plane is covered.
 *
 * Both compare with memcmp. The reference is the scalar vif_filter1d_s():
 * no test here calls vmaf_init_cpu(), and the mask below keeps it that way.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "feature/speed_internal.h"
#include "feature/vif_tools.h"
#include "mu_table.h"
#include "test.h"

/* NOLINTBEGIN(modernize-use-nullptr) -- ADR-1138: retain NULL for Windows C
 * support and upstream-compatible C test conventions. */

#define SF_NUM_SCALES 4 /* NUM_SCALES, speed.c */
#define SF_DST_GUARD (-12345.f)
#define SF_PATTERNS 5u
#define SF_LEN(a) (sizeof(a) / sizeof((a)[0]))

typedef struct SfSize {
    int w;
    int h;
} SfSize;

typedef struct SfBuffers {
    float *allocation; /* src, one float earlier for the unaligned layout */
    float *src;
    float *full; /* vif_filter1d_s() output */
    float *tmp;
    float *expected;
    float *actual;
    int src_stride; /* floats */
    int full_stride;
    int dst_stride;
    size_t src_count;
    size_t dst_count;
} SfBuffers;

static const float sf_kernelscales[] = {
    0.1f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 360.0f / 97.0f, 4.0f,
};

static void sf_free(SfBuffers *b)
{
    free(b->allocation);
    free(b->full);
    free(b->tmp);
    free(b->expected);
    free(b->actual);
}

/* layout 0: tight strides, aligned source. layout 1: padded strides and a
 * source one float past the allocation's alignment. */
static int sf_alloc(SfBuffers *b, int w, int h, unsigned layout)
{
    b->src_stride = w + (layout ? 3 : 0);
    b->full_stride = w + (layout ? 7 : 0);
    b->dst_stride = w / 16 + (layout ? 5 : 0);
    b->src_count = (size_t)(h - 1) * (size_t)b->src_stride + (size_t)w;
    b->dst_count = (size_t)(h / 16) * (size_t)b->dst_stride;
    b->allocation = malloc((b->src_count + layout) * sizeof(float));
    b->src = b->allocation ? b->allocation + layout : NULL;
    b->full = malloc((size_t)h * (size_t)b->full_stride * sizeof(float));
    b->tmp = malloc((size_t)w * sizeof(float));
    b->expected = malloc(b->dst_count * sizeof(float));
    b->actual = malloc(b->dst_count * sizeof(float));
    return b->src && b->full && b->tmp && b->expected && b->actual;
}

static float sf_pattern_value(unsigned pattern, uint32_t state, int i, int j, SfSize size)
{
    const int corner_row = i == 0 || i == size.h - 1;
    const int corner_col = j == 0 || j == size.w - 1;

    switch (pattern) {
    case 0:
        return (float)((int)(state & 65535u) - 32768) / 128.f;
    case 1:
        return 127.5f;
    case 2:
        return ((i + j) & 1) ? 255.f : 0.f;
    case 3:
        return corner_row && corner_col ? 255.f : 0.f;
    default:
        return (float)(i * size.w + j) / 257.f;
    }
}

/* Padding stays NaN: a pass that reads it poisons its output. */
static void sf_fill(float *src, size_t count, int stride, SfSize size, unsigned pattern,
                    uint32_t *state)
{
    for (size_t i = 0; i < count; i++)
        src[i] = NAN;
    for (int i = 0; i < size.h; i++) {
        for (int j = 0; j < size.w; j++) {
            *state = *state * 1664525u + 1013904223u;
            src[(ptrdiff_t)i * stride + j] = sf_pattern_value(pattern, *state, i, j, size);
        }
    }
}

/* One filter width on the filled source: the two calls into expected, the
 * fused call into actual. Returns 0 when the bits match. */
static int sf_compare(SfBuffers *b, SfSize size, float kernelscale)
{
    const int fwidth = vif_get_filter_size(1, kernelscale);
    float filter[128];

    if (fwidth / 2 >= size.w || fwidth / 2 >= size.h)
        return 0; /* the mirror needs the half width inside the plane */

    speed_get_antialias_filter(filter, SF_NUM_SCALES, kernelscale);
    for (size_t i = 0; i < b->dst_count; i++) {
        b->expected[i] = SF_DST_GUARD;
        b->actual[i] = SF_DST_GUARD;
    }
    vif_filter1d_s(filter, b->src, b->full, b->tmp, size.w, size.h,
                   b->src_stride * (int)sizeof(float), b->full_stride * (int)sizeof(float), fwidth);
    vif_dec16_s(b->full, b->expected, size.w, size.h, b->full_stride * (int)sizeof(float),
                b->dst_stride * (int)sizeof(float));
    vif_filter1d_dec16_s(filter, b->src, b->actual, b->tmp, size.w, size.h,
                         b->src_stride * (int)sizeof(float), b->dst_stride * (int)sizeof(float),
                         fwidth);
    if (memcmp(b->expected, b->actual, b->dst_count * sizeof(float)) != 0) {
        (void)fprintf(stderr, "%dx%d, src stride %d, filter %d\n", size.w, size.h, b->src_stride,
                      fwidth);
        return 1;
    }
    return 0;
}

static char *sf_check_layout(SfSize size, unsigned layout, uint32_t *state)
{
    SfBuffers b = {0};
    int mismatches = 0;

    if (!sf_alloc(&b, size.w, size.h, layout)) {
        sf_free(&b);
        return "filter buffer allocation failed";
    }
    for (unsigned pattern = 0; pattern < SF_PATTERNS; pattern++) {
        sf_fill(b.src, b.src_count, b.src_stride, size, pattern, state);
        for (size_t k = 0; k < SF_LEN(sf_kernelscales); k++)
            mismatches += sf_compare(&b, size, sf_kernelscales[k]);
    }
    sf_free(&b);
    mu_assert("decimated filter differs from full filter", mismatches == 0);
    return NULL;
}

static char *test_filter_dec16(void)
{
    static const SfSize sizes[] = {
        /* Netflix/vmaf 76ea5f03, test_speed_filter.c */
        {16, 16},
        {17, 31},
        {31, 17},
        {32, 32},
        {33, 33},
        {63, 65},
        {65, 63},
        {80, 80},
        {81, 95},
        {95, 81},
        {96, 97},
        {97, 96},
        {128, 129},
        {160, 161},
        {320, 180},
        /* Netflix/vmaf cea2b4d8, checkasm check_filter_dec16() */
        {64, 64},
        {255, 63},
        {256, 64},
        /* planes SpEED filters in this tree */
        {81, 83},
        {173, 131},
        {288, 162},
    };
    uint32_t state = 123456789;

    vmaf_set_cpu_flags_mask(0);
    for (size_t s = 0; s < SF_LEN(sizes); s++) {
        for (unsigned layout = 0; layout < 2; layout++)
            mu_assert_msg(sf_check_layout(sizes[s], layout, &state));
    }
    return NULL;
}

/* filter_and_downscale() with the two calls spelled out, prescale 1. */
static void sf_reference_downscale(const SpeedInternalDimensions *dim, float kernelscale,
                                   float *frame, float *scratch, size_t float_stride)
{
    const size_t stride_px = float_stride / sizeof(float);
    float *curr_scale = scratch;
    float *tmpbuf = scratch + stride_px * dim->alloc_height;
    const int w = (int)dim->scaled_width;
    const int h = (int)dim->scaled_height;
    const int down_w = w >> SF_NUM_SCALES;
    const int down_h = h >> SF_NUM_SCALES;
    float taps[128];

    speed_get_antialias_filter(taps, SF_NUM_SCALES, kernelscale);
    vif_filter1d_s(taps, frame, curr_scale, tmpbuf, w, h, (int)float_stride, (int)float_stride,
                   vif_get_filter_size(1, kernelscale));
    vif_dec16_s(curr_scale, frame, w, h, (int)float_stride, (int)float_stride);

    vif_get_filter(taps, SF_NUM_SCALES, kernelscale);
    vif_filter1d_s(taps, frame, curr_scale, tmpbuf, down_w, down_h, (int)float_stride,
                   (int)float_stride, vif_get_filter_size(SF_NUM_SCALES, kernelscale));
    for (int i = 0; i < down_h; i++) {
        for (int j = 0; j < down_w; j++)
            frame[(size_t)i * stride_px + (size_t)j] -= curr_scale[(size_t)i * stride_px + j];
    }
}

static char *sf_check_downscale(SfSize size, float kernelscale, uint32_t *state)
{
    SpeedInternalDimensions dim = {0};
    char method[] = "nearest";
    SpeedInternalOptions opt = {
        .speed_kernelscale = (double)kernelscale,
        .speed_prescale = 1.0,
        .speed_prescale_method = method,
    };
    mu_assert("speed_internal_init_dimensions refused the plane",
              speed_internal_init_dimensions(&dim, size.w, size.h, 1.0) == 0);
    const size_t float_stride = speed_internal_float_stride(dim.alloc_width);
    const size_t frame_floats = (float_stride / sizeof(float)) * dim.alloc_height;
    float *expected = malloc(frame_floats * sizeof(float));
    float *actual = malloc(frame_floats * sizeof(float));
    float *scratch = malloc(2u * frame_floats * sizeof(float));
    int differs = 1;

    if (expected && actual && scratch) {
        sf_fill(expected, frame_floats, (int)(float_stride / sizeof(float)), size, 0, state);
        (void)memcpy(actual, expected, frame_floats * sizeof(float));
        sf_reference_downscale(&dim, kernelscale, expected, scratch, float_stride);
        speed_internal_filter_and_downscale(&dim, &opt, actual, scratch, float_stride);
        differs = memcmp(expected, actual, frame_floats * sizeof(float)) != 0;
    }
    free(expected);
    free(actual);
    free(scratch);
    if (differs)
        (void)fprintf(stderr, "%dx%d, kernelscale %g\n", size.w, size.h, (double)kernelscale);
    mu_assert("speed_internal_filter_and_downscale differs from the two calls", !differs);
    return NULL;
}

static char *test_filter_and_downscale(void)
{
    static const SfSize sizes[] = {{80, 80}, {81, 83}, {173, 131}, {288, 162}, {576, 324}};
    /* speed_kernelscale values the extractors accept (valid_kernelscales). */
    static const float kernelscales[] = {1.0f, 0.5f, 2.0f, 360.0f / 97.0f};
    uint32_t state = 987654321;

    vmaf_set_cpu_flags_mask(0);
    for (size_t s = 0; s < SF_LEN(sizes); s++) {
        for (size_t k = 0; k < SF_LEN(kernelscales); k++)
            mu_assert_msg(sf_check_downscale(sizes[s], kernelscales[k], &state));
    }
    return NULL;
}

char *run_tests(void)
{
    static const MuTest tests[] = {
        MU_TEST(test_filter_dec16),
        MU_TEST(test_filter_and_downscale),
    };
    return mu_run_table(tests, MU_TABLE_LEN(tests));
}

/* NOLINTEND(modernize-use-nullptr) */
