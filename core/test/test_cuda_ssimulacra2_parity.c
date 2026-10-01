/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * ADR-0947 — ssimulacra2 CPU vs. CUDA parity test (round 3); exact since
 * ADR-1433.
 *
 * The ssimulacra2 extractor is implemented independently in
 * core/src/feature/ssimulacra2.c (CPU) and
 * core/src/feature/cuda/ssimulacra2_cuda.c (CUDA).  Both emit the
 * scalar `ssimulacra2` feature.  Since ADR-1391 the CUDA twin runs the
 * whole frame on the device (ssimulacra2/ssimulacra2_device.cu and
 * ssimulacra2/ssimulacra2_blur.cu): YUV conversion, XYB, blurs and
 * downsample reproduce the CPU bit for bit.  Since ADR-1433 the sums of the
 * per-pixel SSIM / edge terms do too: ssim_map() and edge_diff_map() add
 * each term pixel after pixel into one double, and the device returns the
 * bits of that loop (feature/ordered_sum.h).
 *
 * Asserts equality, every frame, across 3 frames on a 256x144 YUV420P 8-bpc
 * fixture (and 960x540 in the `_large` variant), in three forms: a distorted
 * frame, identical frames (every sum is zero), and a frame distorted in its
 * lower third only (each sum starts with a run of zeros).  Before ADR-1433
 * the terms were added in a fixed tree and the score was a few 1e-13 to
 * 7e-11 from the CPU's, so the distorted cases fail on that twin.  256x144
 * runs five pyramid scales (the sixth, 8x5, is below the 8x8 floor, which
 * exercises the early stop); 960x540 runs all six.  Skips cleanly when no
 * CUDA device is visible.
 */

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test.h"

#include "feature/feature_extractor.h"
#include "libvmaf/libvmaf.h"
#include "libvmaf/libvmaf_cuda.h"
#include "libvmaf/picture.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy also proposes the `nullptr` keyword, but MSVC's
 * documented /std:clatest C23 feature set does not include `nullptr` while the
 * required Windows build compiles this TU with cl.exe, and this file mirrors
 * the C spelling of the surface it exercises. ADR-1138. */

#ifndef FIXTURE_W
#define FIXTURE_W 256u
#endif
#ifndef FIXTURE_H
#define FIXTURE_H 144u
#endif
#define FIXTURE_BPC 8u
#define NUM_FRAMES 3u

/* How the distorted picture differs from the reference. */
enum distortion {
    DISTORT_ALL = 0,     /* every pixel */
    DISTORT_NONE,        /* identical pictures */
    DISTORT_LOWER_THIRD, /* rows from two thirds down */
};

static bool row_is_distorted(enum distortion mode, unsigned row, unsigned rows)
{
    if (mode == DISTORT_NONE)
        return false;
    return mode == DISTORT_ALL || row >= rows - rows / 3u;
}

static int fill_ref(VmafPicture *pic, unsigned frame_idx)
{
    int err = vmaf_picture_alloc(pic, VMAF_PIX_FMT_YUV420P, FIXTURE_BPC, FIXTURE_W, FIXTURE_H);
    if (err)
        return err;

    uint8_t *y = (uint8_t *)pic->data[0];
    for (unsigned row = 0; row < pic->h[0]; row++) {
        for (unsigned col = 0; col < pic->w[0]; col++) {
            y[row * pic->stride[0] + col] = (uint8_t)((row + col + frame_idx * 5u) & 0xFFu);
        }
    }
    /* ssimulacra2 reads chroma planes through the YUV->XYB conversion;
     * give them deterministic non-128 values so the score is non-trivial. */
    for (unsigned p = 1; p < 3; p++) {
        uint8_t *plane = (uint8_t *)pic->data[p];
        for (unsigned row = 0; row < pic->h[p]; row++) {
            for (unsigned col = 0; col < pic->w[p]; col++) {
                plane[row * pic->stride[p] + col] =
                    (uint8_t)((row * 2u + col + p * 19u + frame_idx) & 0xFFu);
            }
        }
    }
    return 0;
}

static int fill_dist(VmafPicture *pic, unsigned frame_idx, enum distortion mode)
{
    int err = vmaf_picture_alloc(pic, VMAF_PIX_FMT_YUV420P, FIXTURE_BPC, FIXTURE_W, FIXTURE_H);
    if (err)
        return err;

    uint8_t *y = (uint8_t *)pic->data[0];
    for (unsigned row = 0; row < pic->h[0]; row++) {
        for (unsigned col = 0; col < pic->w[0]; col++) {
            const unsigned base = (row + col + frame_idx * 5u) & 0xFFu;
            const unsigned noise = row_is_distorted(mode, row, pic->h[0]) ?
                                       ((row * 2u + col + frame_idx * 3u) % 13u) :
                                       0u;
            y[row * pic->stride[0] + col] = (uint8_t)((base + noise) & 0xFFu);
        }
    }
    for (unsigned p = 1; p < 3; p++) {
        uint8_t *plane = (uint8_t *)pic->data[p];
        for (unsigned row = 0; row < pic->h[p]; row++) {
            for (unsigned col = 0; col < pic->w[p]; col++) {
                const unsigned base = (row * 2u + col + p * 19u + frame_idx) & 0xFFu;
                const unsigned noise = row_is_distorted(mode, row, pic->h[p]) ?
                                           ((row + col * 3u + frame_idx) % 7u) :
                                           0u;
                plane[row * pic->stride[p] + col] = (uint8_t)((base + noise) & 0xFFu);
            }
        }
    }
    return 0;
}

static int feed_frames(VmafContext *vmaf, enum distortion mode)
{
    for (unsigned i = 0; i < NUM_FRAMES; i++) {
        VmafPicture ref;
        VmafPicture dist;
        int err = fill_ref(&ref, i);
        if (err)
            return err;
        err = fill_dist(&dist, i, mode);
        if (err) {
            vmaf_picture_unref(&ref);
            return err;
        }
        err = vmaf_read_pictures(vmaf, &ref, &dist, i);
        if (err)
            return err;
    }
    return vmaf_read_pictures(vmaf, NULL, NULL, 0);
}

static int read_scores(VmafContext *vmaf, double scores[NUM_FRAMES])
{
    for (unsigned i = 0; i < NUM_FRAMES; i++) {
        int err = vmaf_feature_score_at_index(vmaf, "ssimulacra2", &scores[i], i);
        if (err)
            return err;
    }
    return 0;
}

static char *run_cpu(enum distortion mode, double score[NUM_FRAMES])
{
    int err = 0;
    VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    VmafContext *vmaf = NULL;
    err = vmaf_init(&vmaf, cfg);
    mu_assert("CPU: vmaf_init failed", !err);

    err = vmaf_use_feature(vmaf, "ssimulacra2", NULL);
    mu_assert("CPU: vmaf_use_feature(ssimulacra2) failed", !err);

    err = feed_frames(vmaf, mode);
    mu_assert("CPU: feeding frames failed", !err);
    err = read_scores(vmaf, score);
    mu_assert("CPU: ssimulacra2 score missing", !err);

    err = vmaf_close(vmaf);
    mu_assert("CPU: vmaf_close failed", !err);
    return NULL;
}

/* Scores of the CUDA twin on an initialised CUDA state. */
static char *run_cuda_on(VmafCudaState *cu_state, enum distortion mode, double score[NUM_FRAMES])
{
    VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    VmafContext *vmaf = NULL;
    int err = vmaf_init(&vmaf, cfg);
    mu_assert("CUDA: vmaf_init failed", !err);

    err = vmaf_cuda_import_state(vmaf, cu_state);
    mu_assert("CUDA: vmaf_cuda_import_state failed", !err);

    err = vmaf_use_feature(vmaf, "ssimulacra2_cuda", NULL);
    mu_assert("CUDA: vmaf_use_feature(ssimulacra2_cuda) failed", !err);

    err = feed_frames(vmaf, mode);
    mu_assert("CUDA: feeding frames failed", !err);
    err = read_scores(vmaf, score);
    mu_assert("CUDA: ssimulacra2 score missing", !err);

    err = vmaf_close(vmaf);
    mu_assert("CUDA: vmaf_close failed", !err);
    return NULL;
}

static char *run_cuda(enum distortion mode, double score[NUM_FRAMES], int *device_present)
{
    for (unsigned i = 0; i < NUM_FRAMES; i++)
        score[i] = NAN;
    *device_present = 0;

    VmafCudaState *cu_state = NULL;
    VmafCudaConfiguration cuda_cfg = {0};
    int err = vmaf_cuda_state_init(&cu_state, cuda_cfg);
    if (err != 0 || cu_state == NULL) {
        (void)fprintf(stderr, "[skip: no CUDA device] ");
        return NULL;
    }
    *device_present = 1;

    char *msg = run_cuda_on(cu_state, mode, score);
    if (msg)
        return msg; /* the context may still hold the state: leave it */
    err = vmaf_cuda_state_free(cu_state);
    mu_assert("CUDA: vmaf_cuda_state_free failed", !err);
    return NULL;
}

static char *test_ssimulacra2_cuda_registered(void)
{
    VmafFeatureExtractor *fex = vmaf_get_feature_extractor_by_name("ssimulacra2_cuda");
    mu_assert("ssimulacra2_cuda extractor must be registered", fex != NULL);
    return NULL;
}

static char *test_ssimulacra2_cuda_lifecycle(void)
{
    VmafFeatureExtractor *fex = vmaf_get_feature_extractor_by_name("ssimulacra2_cuda");
    mu_assert("ssimulacra2_cuda extractor must be registered", fex != NULL);
    mu_assert("ssimulacra2_cuda must provide submit", fex->submit != NULL);
    mu_assert("ssimulacra2_cuda must provide collect", fex->collect != NULL);
    mu_assert("ssimulacra2_cuda must be device-resident (extract is NULL)", fex->extract == NULL);
    return NULL;
}

static char *test_ssimulacra2_cuda_context_check(void)
{
    VmafFeatureExtractor *fex = vmaf_get_feature_extractor_by_name("ssimulacra2_cuda");
    mu_assert("ssimulacra2_cuda extractor must be registered", fex != NULL);
    mu_assert("ssimulacra2_cuda declares a context check", fex->context_check != NULL);
    mu_assert("ssimulacra2_cuda falls back to the CPU extractor",
              fex->context_fallback_name && !strcmp(fex->context_fallback_name, "ssimulacra2"));
    return NULL;
}

static char *test_ssimulacra2_cuda_context_bounds(void)
{
    VmafFeatureExtractor *fex = vmaf_get_feature_extractor_by_name("ssimulacra2_cuda");
    mu_assert("ssimulacra2_cuda declares a context check", fex && fex->context_check);
    mu_assert("8x8 4:2:0 is accepted",
              fex->context_check(fex, VMAF_PIX_FMT_YUV420P, 8u, 8u, 8u) == 0);
    mu_assert("4K 4:4:4 10-bit is accepted",
              fex->context_check(fex, VMAF_PIX_FMT_YUV444P, 10u, 3840u, 2160u) == 0);
    mu_assert("7x8 is rejected",
              fex->context_check(fex, VMAF_PIX_FMT_YUV420P, 8u, 7u, 8u) == -ENOTSUP);
    mu_assert("8x7 is rejected",
              fex->context_check(fex, VMAF_PIX_FMT_YUV422P, 8u, 8u, 7u) == -ENOTSUP);
    mu_assert("4:0:0 is rejected",
              fex->context_check(fex, VMAF_PIX_FMT_YUV400P, 8u, 576u, 324u) == -ENOTSUP);
    return NULL;
}

/* The CUDA twin's score of every frame is the CPU's, bit for bit. A missing
 * device skips the case. `cpu_score` returns the CPU's scores. */
static char *check_exact(enum distortion mode, const char *what, double cpu_score[NUM_FRAMES])
{
    double cuda_score[NUM_FRAMES] = {0.0};
    int device_present = 0;

    char *msg = run_cpu(mode, cpu_score);
    if (msg)
        return msg;
    msg = run_cuda(mode, cuda_score, &device_present);
    if (msg)
        return msg;
    if (!device_present) {
        mu_skipped = 1;
        return NULL;
    }

    for (unsigned i = 0; i < NUM_FRAMES; i++) {
        mu_assert("CPU ssimulacra2 score is non-finite", isfinite(cpu_score[i]));
        if (cpu_score[i] != cuda_score[i]) {
            (void)fprintf(stderr, "\nssimulacra2 %s: frame=%u cpu=%.17g cuda=%.17g delta=%.3e\n",
                          what, i, cpu_score[i], cuda_score[i], fabs(cpu_score[i] - cuda_score[i]));
        }
        mu_assert("ssimulacra2_cuda differs from the CPU extractor (ADR-1433)",
                  cpu_score[i] == cuda_score[i]);
    }
    return NULL;
}

static char *test_ssimulacra2_cpu_cuda_exact(void)
{
    double cpu_score[NUM_FRAMES] = {0.0};
    mu_assert_msg(check_exact(DISTORT_ALL, "distorted", cpu_score));
    mu_assert("ssimulacra2 fixture frames must score differently",
              mu_skipped || (cpu_score[0] != cpu_score[1] && cpu_score[1] != cpu_score[2]));
    return NULL;
}

/* Every term of every sum is zero. */
static char *test_ssimulacra2_identical_frames_exact(void)
{
    double cpu_score[NUM_FRAMES] = {0.0};
    mu_assert_msg(check_exact(DISTORT_NONE, "identical", cpu_score));
    mu_assert("identical pictures must score 100", mu_skipped || cpu_score[0] == 100.0);
    return NULL;
}

/* The sums start with a run of zero terms and pick up in the last third. */
static char *test_ssimulacra2_lower_third_exact(void)
{
    double cpu_score[NUM_FRAMES] = {0.0};
    return check_exact(DISTORT_LOWER_THIRD, "lower third", cpu_score);
}

char *run_tests(void)
{
    mu_run_test(test_ssimulacra2_cuda_registered);
    mu_run_test(test_ssimulacra2_cuda_lifecycle);
    mu_run_test(test_ssimulacra2_cuda_context_check);
    mu_run_test(test_ssimulacra2_cuda_context_bounds);
    mu_run_test(test_ssimulacra2_cpu_cuda_exact);
    mu_run_test(test_ssimulacra2_identical_frames_exact);
    mu_run_test(test_ssimulacra2_lower_third_exact);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
