/**
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: EUPL-1.2
 *
 *  End-to-end tests of the mobilesal extractor with the shipped saliency
 *  students (ADR-1540): frames whose sides are not multiples of 8 are padded
 *  to a size the students accept, and the score is the map's mean over the
 *  frame's own area. Linked against libvmaf so the public surface is
 *  exercised.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "mu_table.h"
#include "test.h"

#include "libvmaf/dnn.h"
#include "libvmaf/feature.h"
#include "libvmaf/libvmaf.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy also proposes the `nullptr` keyword, but MSVC's
 * documented /std:clatest C23 feature set does not include `nullptr` while the
 * required Windows build compiles this TU with cl.exe, and this file mirrors
 * the C spelling of the surface it exercises. ADR-1138. */

/* Paths are relative to workdir = project root (set in meson.build). */
#define STUDENT_V1 "model/tiny/saliency_student_v1.onnx"
#define STUDENT_V2 "model/tiny/saliency_student_v2.onnx"

/* A bright disc on a textured background: something for the student to
 * find, in every plane of a YUV420P picture. */
static void fill_plane(VmafPicture *pic, unsigned p)
{
    const unsigned w = pic->w[p];
    const unsigned h = pic->h[p];
    for (unsigned y = 0; y < h; ++y) {
        uint8_t *row = (uint8_t *)pic->data[p] + (size_t)y * pic->stride[p];
        for (unsigned x = 0; x < w; ++x) {
            const int dx = (int)(2u * x) - (int)w;
            const int dy = (int)(2u * y) - (int)h;
            const int r2 = (int)((w * w + h * h) / 16u);
            const unsigned texture = 60u + ((x * 7u + y * 13u) % 40u);
            row[x] = (uint8_t)(p != 0u ? 128u : (dx * dx + dy * dy < r2 ? 230u : texture));
        }
    }
}

static int read_frame(VmafContext *ctx, unsigned w, unsigned h, unsigned index)
{
    VmafPicture ref = {0};
    VmafPicture dist = {0};
    int rc = vmaf_picture_alloc(&ref, VMAF_PIX_FMT_YUV420P, 8, w, h);
    if (rc)
        return rc;
    rc = vmaf_picture_alloc(&dist, VMAF_PIX_FMT_YUV420P, 8, w, h);
    if (rc) {
        (void)vmaf_picture_unref(&ref);
        return rc;
    }
    for (unsigned p = 0; p < 3u; ++p) {
        fill_plane(&ref, p);
        fill_plane(&dist, p);
    }
    return vmaf_read_pictures(ctx, &ref, &dist, index);
}

/* Score two w x h frames with @p model and require a finite saliency_mean
 * in [0, 1] on both. */
static char *expect_saliency(const char *model, unsigned w, unsigned h)
{
    VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE, .n_threads = 1, .n_subsample = 1};
    VmafContext *ctx = NULL;
    mu_assert("vmaf_init", vmaf_init(&ctx, cfg) == 0);
    VmafFeatureDictionary *opts = NULL;
    mu_assert("model_path option", vmaf_feature_dictionary_set(&opts, "model_path", model) == 0);
    mu_assert("mobilesal registered", vmaf_use_feature(ctx, "mobilesal", opts) == 0);
    int rc = read_frame(ctx, w, h, 0u);
    if (rc == 0)
        rc = read_frame(ctx, w, h, 1u);
    const int flush_rc = rc == 0 ? vmaf_read_pictures(ctx, NULL, NULL, 0u) : rc;
    double mean[2] = {-1.0, -1.0};
    int got = 0;
    for (unsigned i = 0; i < 2u; ++i)
        got += vmaf_feature_score_at_index(ctx, "saliency_mean", &mean[i], i) == 0;
    (void)vmaf_close(ctx);
    mu_assert("ADR-1540: every frame is scored", rc == 0 && flush_rc == 0 && got == 2);
    for (unsigned i = 0; i < 2u; ++i) {
        mu_assert("saliency_mean is finite and in [0, 1]",
                  isfinite(mean[i]) && mean[i] >= 0.0 && mean[i] <= 1.0);
    }
    return NULL;
}

/* Positive: the Netflix pair's 576x324 (324 = 8 * 40 + 4) runs. */
static char *test_student_v2_576x324(void)
{
    if (!vmaf_dnn_available())
        return NULL;
    return expect_saliency(STUDENT_V2, 576u, 324u);
}

static char *test_student_v1_576x324(void)
{
    if (!vmaf_dnn_available())
        return NULL;
    return expect_saliency(STUDENT_V1, 576u, 324u);
}

/* Boundary: both sides padded. */
static char *test_student_v2_578x330(void)
{
    if (!vmaf_dnn_available())
        return NULL;
    return expect_saliency(STUDENT_V2, 578u, 330u);
}

/* Boundary: a frame smaller than one multiple. */
static char *test_student_v2_6x6(void)
{
    if (!vmaf_dnn_available())
        return NULL;
    return expect_saliency(STUDENT_V2, 6u, 6u);
}

/* Control: a size that needs no padding keeps running. */
static char *test_student_v2_576x320(void)
{
    if (!vmaf_dnn_available())
        return NULL;
    return expect_saliency(STUDENT_V2, 576u, 320u);
}

char *run_tests(void)
{
    static const MuTest tests[] = {
        MU_TEST(test_student_v2_576x324), MU_TEST(test_student_v1_576x324),
        MU_TEST(test_student_v2_578x330), MU_TEST(test_student_v2_6x6),
        MU_TEST(test_student_v2_576x320),
    };
    return mu_run_table(tests, MU_TABLE_LEN(tests));
}

/* NOLINTEND(modernize-use-nullptr) */
