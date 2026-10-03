/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * ADR-1426 — device-free replay of the ciede CUDA kernel against the CPU
 * extractor.
 *
 * cuda/integer_ciede/ciede_device.h holds the arithmetic every thread of
 * ciede_score.cu runs: ciede.c's get_lab_color() and ciede2000(), statement
 * for statement, with the float-to-double promotions C applies written out.
 * This test compiles the header for the host, where its math functions are
 * the CPU extractor's own (glibc), and checks that a frame replayed pixel by
 * pixel and added in raster order gives the CPU extractor's score bit for
 * bit: 8-, 10-, 12- and 16-bit input, 4:2:0, 4:2:2 and 4:4:4, odd sizes.
 *
 * It also pins the two facts about the math library the header relies on:
 * powf(25, 7) and pow(25, 7) are the constants the header spells out. The
 * squares are products in ciede.c and in the header alike (ADR-1467), so no
 * power function is involved in them.
 *
 * A float where the reference computes in double, a float overload of
 * atan2 / sin / cos / exp, or a sum in another order fails the replay, which
 * is how the twin differed from the CPU before ADR-1426 (1.1e-5). What a
 * host replay cannot see is the device's math library; test_cuda_ciede_parity
 * bounds that on an NVIDIA device.
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test.h"
#include "float_bits.h"

#include "feature/cuda/integer_ciede/ciede_device.h"
#include "libvmaf/libvmaf.h"
#include "libvmaf/picture.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. ADR-1138. */

typedef struct Case {
    unsigned w;
    unsigned h;
    unsigned bpc;
    enum VmafPixelFormat pix_fmt;
    /* The lower half of the distorted picture is one least significant level
     * off instead of several 8-bit levels. */
    bool fine_lower_half;
} Case;

/* Deterministic position hash. */
static unsigned position_hash(unsigned plane, unsigned row, unsigned col, unsigned salt)
{
    unsigned x = row * 73856093u ^ col * 19349663u ^ (salt * 4u + plane) * 83492791u;
    x ^= x >> 13;
    x *= 0x5bd1e995u;
    x ^= x >> 15;
    return x;
}

/* Sample in 8-bit levels: every plane varies, and the distorted picture is
 * the reference plus an error of a few levels, so the pixel differences span
 * the range of the formula (near-neutral and saturated colours, small and
 * large hue differences). */
static unsigned fixture_sample(unsigned plane, unsigned row, unsigned col, bool distorted)
{
    const unsigned lo = (plane == 0u) ? 16u : 40u;
    const unsigned span = (plane == 0u) ? 220u : 176u;
    unsigned v = lo + position_hash(plane, row >> 1, col >> 1, 1u) % span;
    if (distorted)
        v += position_hash(plane, row, col, 2u) % 9u;
    return v;
}

static void put_sample(VmafPicture *pic, unsigned plane, unsigned row, unsigned col, unsigned v)
{
    uint8_t *line = (uint8_t *)pic->data[plane] + (size_t)row * (size_t)pic->stride[plane];
    if (pic->bpc <= 8u) {
        line[col] = (uint8_t)v;
    } else {
        ((uint16_t *)line)[col] = (uint16_t)v;
    }
}

static unsigned get_sample(const VmafPicture *pic, unsigned plane, unsigned row, unsigned col)
{
    const uint8_t *line =
        (const uint8_t *)pic->data[plane] + (size_t)row * (size_t)pic->stride[plane];
    if (pic->bpc <= 8u)
        return line[col];
    return ((const uint16_t *)line)[col];
}

static int fill_picture(VmafPicture *pic, const Case *c, bool distorted)
{
    int err = vmaf_picture_alloc(pic, c->pix_fmt, c->bpc, c->w, c->h);
    if (err)
        return err;
    const unsigned gain = 1u << (c->bpc - 8u);
    for (unsigned p = 0; p < 3; p++) {
        for (unsigned row = 0; row < pic->h[p]; row++) {
            const bool fine = c->fine_lower_half && row >= pic->h[p] / 2u;
            for (unsigned col = 0; col < pic->w[p]; col++) {
                unsigned v = fixture_sample(p, row, col, distorted && !fine) * gain;
                if (distorted && fine)
                    v += position_hash(p, row, col, 3u) & 1u;
                put_sample(pic, p, row, col, v);
            }
        }
    }
    return 0;
}

/* The kernel's frame: each luma position with the chroma sample
 * scale_chroma_planes() would put there, the values in raster order. */
static double replay_score(const VmafPicture *ref, const VmafPicture *dis, const Case *c,
                           float *terms)
{
    const unsigned ss_hor = (c->pix_fmt != VMAF_PIX_FMT_YUV444P) ? 1u : 0u;
    const unsigned ss_ver = (c->pix_fmt == VMAF_PIX_FMT_YUV420P) ? 1u : 0u;
    for (unsigned y = 0; y < c->h; y++) {
        for (unsigned x = 0; x < c->w; x++) {
            const unsigned cx = ss_hor ? (x >> 1) : x;
            const unsigned cy = ss_ver ? (y >> 1) : y;
            terms[(size_t)y * c->w + x] = ciede_pixel(
                (float)get_sample(ref, 0u, y, x), (float)get_sample(ref, 1u, cy, cx),
                (float)get_sample(ref, 2u, cy, cx), (float)get_sample(dis, 0u, y, x),
                (float)get_sample(dis, 1u, cy, cx), (float)get_sample(dis, 2u, cy, cx), c->bpc);
        }
    }
    const double de00_sum = ciede_frame_sum(terms, (size_t)c->w * c->h);
    return 45. - 20. * log10(de00_sum / (c->w * c->h));
}

/* The CPU extractor's ciede2000 for the pair, through the public API; the
 * pictures are handed over to libvmaf. */
static char *cpu_score(VmafPicture *ref, VmafPicture *dis, double *score)
{
    VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    VmafContext *vmaf = NULL;
    mu_assert("vmaf_init failed", !vmaf_init(&vmaf, cfg));
    mu_assert("vmaf_use_feature(ciede) failed", !vmaf_use_feature(vmaf, "ciede", NULL));
    mu_assert("vmaf_read_pictures failed", !vmaf_read_pictures(vmaf, ref, dis, 0u));
    mu_assert("vmaf_read_pictures(EOS) failed", !vmaf_read_pictures(vmaf, NULL, NULL, 0));
    mu_assert("no ciede2000 score", !vmaf_feature_score_at_index(vmaf, "ciede2000", score, 0u));
    mu_assert("vmaf_close failed", !vmaf_close(vmaf));
    return NULL;
}

static char *check_case(const Case *c)
{
    VmafPicture ref;
    VmafPicture dis;
    mu_assert("fill reference failed", !fill_picture(&ref, c, false));
    mu_assert("fill distorted failed", !fill_picture(&dis, c, true));

    float *terms = malloc((size_t)c->w * c->h * sizeof(*terms));
    mu_assert("allocation failed", terms != NULL);
    /* Before the pictures are handed to libvmaf, which takes them over. */
    const double replay = replay_score(&ref, &dis, c, terms);
    free(terms);

    double cpu = NAN;
    char *msg = cpu_score(&ref, &dis, &cpu);
    if (msg)
        return msg;
    const bool identical = vmaf_test_identical_f64(cpu, replay);
    if (!identical) {
        (void)fprintf(stderr, "\n%ux%u %u-bit fmt %d: cpu=%.17g replay=%.17g delta=%.3e\n", c->w,
                      c->h, c->bpc, (int)c->pix_fmt, cpu, replay, fabs(cpu - replay));
    }
    mu_assert("the header's replay must equal the CPU extractor's ciede2000", identical);
    return NULL;
}

static char *test_replay_equals_cpu_420(void)
{
    static const Case cases[4] = {
        {96u, 64u, 8u, VMAF_PIX_FMT_YUV420P, false},
        {96u, 64u, 10u, VMAF_PIX_FMT_YUV420P, false},
        {96u, 64u, 12u, VMAF_PIX_FMT_YUV420P, false},
        {96u, 64u, 16u, VMAF_PIX_FMT_YUV420P, false},
    };
    char *msg = NULL;
    for (int i = 0; i < 4 && !msg; i++)
        msg = check_case(&cases[i]);
    return msg;
}

static char *test_replay_equals_cpu_other_layouts(void)
{
    static const Case cases[4] = {
        {97u, 65u, 8u, VMAF_PIX_FMT_YUV420P, false},
        {70u, 46u, 8u, VMAF_PIX_FMT_YUV422P, false},
        {70u, 46u, 10u, VMAF_PIX_FMT_YUV444P, false},
        {33u, 1u, 8u, VMAF_PIX_FMT_YUV444P, false},
    };
    char *msg = NULL;
    for (int i = 0; i < 4 && !msg; i++)
        msg = check_case(&cases[i]);
    return msg;
}

/* The sum is its order. In a small frame every addition into the double is
 * exact and any order gives the same total; here the upper half adds up to
 * more than 2^20 and the lower half then contributes values near 2^-9, whose
 * low bits no longer fit, so each addition rounds and only the reference's
 * raster order reproduces its total. */
static char *test_replay_equals_cpu_where_the_sum_rounds(void)
{
    const Case c = {640u, 360u, 16u, VMAF_PIX_FMT_YUV420P, true};
    return check_case(&c);
}

/* What ciede_device.h writes out instead of calling the math library. The
 * arguments are volatile so the compiler cannot fold the calls. */
static char *test_math_library_facts(void)
{
    volatile float base = 25.0f;
    volatile float seven = 7.0f;
    volatile double dbase = 25.0;
    volatile double dseven = 7.0;
    mu_assert("powf(25, 7) must be the float CIEDE_POWF_25_7",
              powf(base, seven) == CIEDE_POWF_25_7);
    mu_assert("pow(25, 7) must be 6103515625", pow(dbase, dseven) == CIEDE_POW_25_7);
    mu_assert("CIEDE_PI must be the reference's pi", CIEDE_PI == 3.14159265358979323846);
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_replay_equals_cpu_420);
    mu_run_test(test_replay_equals_cpu_other_layouts);
    mu_run_test(test_replay_equals_cpu_where_the_sum_rounds);
    mu_run_test(test_math_library_facts);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
