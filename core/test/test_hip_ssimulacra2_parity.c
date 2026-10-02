/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * ssimulacra2 CPU vs. HIP: the twin returns the CPU's score bit for bit
 * (ADR-1445; first added as a tolerance test, ADR-0958 round 4; the twin is
 * device-resident since ADR-1390).
 *
 * `ssimulacra2_hip` runs the whole frame on the device: YUV -> linear RGB,
 * XYB, the recursive Gaussian blurs, the per-pixel SSIM / edge-difference
 * terms in fp64 and the downsample, with one readback per frame in
 * collect(). The CPU adds each term pixel after pixel into one double; the
 * twin returns the bits of those loops (feature/ordered_sum.h), so every
 * per-frame score is compared with ==. Before ADR-1445 the twin evaluated
 * the terms as fp32 pairs and added them in a fixed tree: it was within
 * 1e-9 of the CPU and equal to it on no measured frame of real content.
 *
 * The cases cover what the device code branches on: the nearest-neighbour
 * chroma mapping of an odd 4:2:0 size (chroma is not exactly half the luma
 * size), 4:2:2 and 4:4:4, 10- and 12-bit samples, the full-range matrices,
 * and an 8x8 frame (one scale); and what the sums branch on: identical
 * pictures (every sum is zero) and a picture distorted in its lower third
 * only (a sum that stays zero over most of the plane and then grows through
 * many binades). Every frame has its own content: the twin is
 * submit/collect, so frame N is collected after frame N + 1 is submitted
 * and a readback keyed to the wrong frame fails here.
 *
 * Also checked: the ADR-1324 / ADR-1359 context check (4:0:0 and sides
 * below 8 go to the CPU extractor) and init rejecting 4:0:0.
 *
 * Skip behaviour: no HIP device, or a build without device kernels
 * (-ENOSYS), prints "[skip: ...]" and passes.
 */

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test.h"

#include "feature/feature_extractor.h"
#include "libvmaf/libvmaf.h"
#include "libvmaf/libvmaf_hip.h"
#include "libvmaf/picture.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy also proposes the `nullptr` keyword, but MSVC's
 * documented /std:clatest C23 feature set does not include `nullptr` while the
 * required Windows build compiles this TU with cl.exe, and this test mirrors
 * the C spelling of the surface it exercises. ADR-1138. */

#ifndef FIXTURE_W
#define FIXTURE_W 256u
#endif
#ifndef FIXTURE_H
#define FIXTURE_H 144u
#endif
#define PARITY_FRAMES 3u

/* How the distorted picture differs from the reference. */
enum {
    DISTORT_ALL = 1u,        /* every pixel */
    DISTORT_NONE = 0u,       /* identical pictures */
    DISTORT_LOWER_THIRD = 2u /* rows from two thirds down */
};

typedef struct Ss2Case {
    const char *name;
    enum VmafPixelFormat pix_fmt;
    unsigned bpc;
    unsigned w;
    unsigned h;
    const char *yuv_matrix; /* option value, or NULL for the default */
    unsigned distortion;    /* DISTORT_* */
} Ss2Case;

static const Ss2Case cases[] = {
    {"420 8-bit fixture", VMAF_PIX_FMT_YUV420P, 8u, FIXTURE_W, FIXTURE_H, NULL, DISTORT_ALL},
    {"420 8-bit odd 131x77", VMAF_PIX_FMT_YUV420P, 8u, 131u, 77u, NULL, DISTORT_ALL},
    {"422 10-bit bt601_full", VMAF_PIX_FMT_YUV422P, 10u, 96u, 64u, "3", DISTORT_ALL},
    {"444 12-bit bt709_full", VMAF_PIX_FMT_YUV444P, 12u, 64u, 40u, "2", DISTORT_ALL},
    {"420 8-bit 8x8 bt601_limited", VMAF_PIX_FMT_YUV420P, 8u, 8u, 8u, "1", DISTORT_ALL},
    {"identical pictures", VMAF_PIX_FMT_YUV420P, 8u, FIXTURE_W, FIXTURE_H, NULL, DISTORT_NONE},
    {"lower third distorted", VMAF_PIX_FMT_YUV420P, 8u, FIXTURE_W, FIXTURE_H, NULL,
     DISTORT_LOWER_THIRD},
};
#define N_CASES (sizeof(cases) / sizeof(cases[0]))

/* A smooth, frame-dependent picture; `salt` 1 adds a small bounded
 * distortion, so the scores sit in ssimulacra2's usual range. */
static unsigned sample_at(unsigned plane, unsigned row, unsigned col, unsigned frame, unsigned salt)
{
    const unsigned base = (plane == 0u) ? 64u : 104u;
    const unsigned ramp = (row * 3u + col * 5u + frame * 11u + plane * 7u) % 96u;
    const unsigned check = (((row >> 2u) + (col >> 2u) + frame) & 1u) * 24u;
    unsigned v = base + ramp + check;
    if (salt != 0u) {
        const unsigned h = (row * 2654435761u) ^ (col * 40503u) ^ (frame * 2246822519u);
        v = v + ((h >> 7u) % 9u) - 4u;
    }
    return v;
}

/* `distortion` is DISTORT_NONE for the reference picture. */
static void fill_plane(VmafPicture *pic, unsigned plane, unsigned frame, unsigned distortion)
{
    const unsigned shift = pic->bpc - 8u;
    const unsigned rows = pic->h[plane];
    for (unsigned row = 0u; row < rows; row++) {
        uint8_t *line = (uint8_t *)pic->data[plane] + (size_t)row * pic->stride[plane];
        const unsigned salt =
            (distortion == DISTORT_LOWER_THIRD) ? (row >= rows - rows / 3u ? 1u : 0u) : distortion;
        for (unsigned col = 0u; col < pic->w[plane]; col++) {
            const unsigned v = sample_at(plane, row, col, frame, salt) << shift;
            if (pic->bpc > 8u) {
                ((uint16_t *)line)[col] = (uint16_t)v;
            } else {
                line[col] = (uint8_t)v;
            }
        }
    }
}

static int alloc_filled(VmafPicture *pic, const Ss2Case *c, unsigned frame, unsigned distortion)
{
    const int err = vmaf_picture_alloc(pic, c->pix_fmt, c->bpc, c->w, c->h);
    if (err)
        return err;
    for (unsigned plane = 0u; plane < 3u; plane++)
        fill_plane(pic, plane, frame, distortion);
    return 0;
}

static int feed_frames(VmafContext *vmaf, const Ss2Case *c)
{
    int err = 0;
    for (unsigned f = 0u; f < PARITY_FRAMES && !err; f++) {
        VmafPicture ref;
        VmafPicture dist;
        err = alloc_filled(&ref, c, f, DISTORT_NONE);
        if (err)
            break;
        err = alloc_filled(&dist, c, f, c->distortion);
        if (err) {
            (void)vmaf_picture_unref(&ref);
            break;
        }
        err = vmaf_read_pictures(vmaf, &ref, &dist, f);
    }
    if (!err)
        err = vmaf_read_pictures(vmaf, NULL, NULL, 0u);
    return err;
}

/* Registers `feature` with the case's options, runs the frames and reads the
 * scores back. -ENOSYS reaches the caller unchanged (scaffold build). */
static int score_case(VmafContext *vmaf, const Ss2Case *c, const char *feature,
                      double scores[PARITY_FRAMES])
{
    VmafFeatureDictionary *opts = NULL;
    int err = 0;
    if (c->yuv_matrix != NULL)
        err = vmaf_feature_dictionary_set(&opts, "yuv_matrix", c->yuv_matrix);
    if (!err)
        err = vmaf_use_feature(vmaf, feature, opts);
    if (!err)
        err = feed_frames(vmaf, c);
    for (unsigned f = 0u; f < PARITY_FRAMES && !err; f++)
        err = vmaf_feature_score_at_index(vmaf, "ssimulacra2", &scores[f], f);
    return err;
}

static int run_cpu(const Ss2Case *c, double scores[PARITY_FRAMES])
{
    VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    VmafContext *vmaf = NULL;
    int err = vmaf_init(&vmaf, cfg);
    if (err)
        return err;
    err = score_case(vmaf, c, "ssimulacra2", scores);
    const int close_err = vmaf_close(vmaf);
    return err ? err : close_err;
}

/* -ENODEV: no HIP device; -ENOSYS: scaffold build. */
static int run_hip(const Ss2Case *c, double scores[PARITY_FRAMES])
{
    VmafHipState *hip_state = NULL;
    VmafHipConfiguration hip_cfg = {.device_index = -1};
    int err = vmaf_hip_state_init(&hip_state, hip_cfg);
    if (err != 0 || hip_state == NULL)
        return -ENODEV;
    VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    VmafContext *vmaf = NULL;
    err = vmaf_init(&vmaf, cfg);
    if (!err)
        err = vmaf_hip_import_state(vmaf, hip_state);
    if (!err)
        err = score_case(vmaf, c, "ssimulacra2_hip", scores);
    const int close_err = (vmaf != NULL) ? vmaf_close(vmaf) : 0;
    vmaf_hip_state_free(&hip_state);
    return err ? err : close_err;
}

/* Frames of one case whose HIP score is not the CPU's; each one is logged. */
static unsigned case_mismatches(const Ss2Case *c, const double cpu[PARITY_FRAMES],
                                const double hip[PARITY_FRAMES])
{
    unsigned mismatches = 0u;
    for (unsigned f = 0u; f < PARITY_FRAMES; f++) {
        if (isfinite(cpu[f]) && cpu[f] == hip[f])
            continue;
        mismatches++;
        (void)fprintf(stderr, "\n%s frame %u: cpu=%.17g hip=%.17g delta=%.3e", c->name, f, cpu[f],
                      hip[f], fabs(cpu[f] - hip[f]));
    }
    return mismatches;
}

static char *test_ssimulacra2_hip_registered(void)
{
    VmafFeatureExtractor *fex = vmaf_get_feature_extractor_by_name("ssimulacra2_hip");
    mu_assert("ssimulacra2_hip extractor must be registered", fex != NULL);
    mu_assert("ssimulacra2_hip name matches", !strcmp(fex->name, "ssimulacra2_hip"));
    return NULL;
}

static char *test_ssimulacra2_cpu_hip_identical(void)
{
    unsigned mismatches = 0u;
    for (size_t i = 0u; i < N_CASES; i++) {
        double cpu[PARITY_FRAMES] = {0.0};
        double hip[PARITY_FRAMES] = {0.0};
        mu_assert("CPU ssimulacra2 run failed", run_cpu(&cases[i], cpu) == 0);
        const int err = run_hip(&cases[i], hip);
        if (err == -ENODEV || err == -ENOSYS) {
            (void)fprintf(stderr, "[skip: %s] ",
                          err == -ENODEV ? "no HIP device" : "HIP scaffold ENOSYS");
            return NULL;
        }
        mu_assert("HIP ssimulacra2 run failed", err == 0);
        mismatches += case_mismatches(&cases[i], cpu, hip);
        if (cases[i].distortion == DISTORT_NONE)
            mu_assert("identical pictures must score 100", cpu[0] == 100.0);
    }
    (void)fprintf(stderr, "[%zu cases x %u frames, %u differ] ", N_CASES, PARITY_FRAMES,
                  mismatches);
    mu_assert("ssimulacra2_hip is not bit-identical to the CPU ssimulacra2", mismatches == 0u);
    return NULL;
}

static char *check_context_meta(VmafFeatureExtractor *fex)
{
    mu_assert("ssimulacra2_hip extractor must be registered", fex != NULL);
    mu_assert("ssimulacra2_hip declares a context check", fex->context_check != NULL);
    mu_assert("ssimulacra2_hip falls back to the CPU extractor",
              fex->context_fallback_name != NULL &&
                  !strcmp(fex->context_fallback_name, "ssimulacra2"));
    return NULL;
}

static char *check_context_resolutions(VmafFeatureExtractor *fex)
{
    mu_assert("8x8 4:2:0 runs on the twin",
              fex->context_check(fex, VMAF_PIX_FMT_YUV420P, 8u, 8u, 8u) == 0);
    mu_assert("3840x2160 4:4:4 10-bit runs on the twin",
              fex->context_check(fex, VMAF_PIX_FMT_YUV444P, 10u, 3840u, 2160u) == 0);
    mu_assert("7x8 goes to the CPU",
              fex->context_check(fex, VMAF_PIX_FMT_YUV420P, 8u, 7u, 8u) == -ENOTSUP);
    mu_assert("8x7 goes to the CPU",
              fex->context_check(fex, VMAF_PIX_FMT_YUV422P, 8u, 8u, 7u) == -ENOTSUP);
    mu_assert("4:0:0 goes to the CPU",
              fex->context_check(fex, VMAF_PIX_FMT_YUV400P, 8u, 576u, 324u) == -ENOTSUP);
    return NULL;
}

/* ADR-1324 / ADR-1359: what init rejects goes to the CPU extractor when the
 * twin was picked for a model or a `--backend hip` request. */
static char *test_ssimulacra2_hip_context_check(void)
{
    VmafFeatureExtractor *fex = vmaf_get_feature_extractor_by_name("ssimulacra2_hip");
    char *msg = check_context_meta(fex);
    if (msg)
        return msg;
    return check_context_resolutions(fex);
}

/* 4:0:0 has no chroma to convert; init refuses it before touching the
 * device (a scaffold build answers -ENOSYS first). */
static char *test_ssimulacra2_hip_rejects_400(void)
{
    VmafFeatureExtractor *fex = vmaf_get_feature_extractor_by_name("ssimulacra2_hip");
    mu_assert("ssimulacra2_hip extractor must be registered", fex != NULL);
    VmafFeatureExtractorContext *ctx = NULL;
    mu_assert("context create failed", vmaf_feature_extractor_context_create(&ctx, fex, NULL) == 0);
    const int err = vmaf_feature_extractor_context_init(ctx, VMAF_PIX_FMT_YUV400P, 8u, 64u, 64u);
    (void)vmaf_feature_extractor_context_destroy(ctx);
    mu_assert("4:0:0 must be rejected at init", err == -EINVAL || err == -ENOSYS);
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_ssimulacra2_hip_registered);
    mu_run_test(test_ssimulacra2_hip_context_check);
    mu_run_test(test_ssimulacra2_hip_rejects_400);
    mu_run_test(test_ssimulacra2_cpu_hip_identical);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
