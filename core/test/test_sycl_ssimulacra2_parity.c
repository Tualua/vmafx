/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * SSIMULACRA 2 CPU vs. SYCL parity (first added as a 5e-3 test, ADR-0957;
 * equality since ADR-1446).
 *
 * The extractor is ssimulacra2.c on the CPU (registered as "ssimulacra2")
 * and ssimulacra2_sycl.cpp::vmaf_fex_ssimulacra2_sycl on SYCL; both emit the
 * feature "ssimulacra2". Since ADR-1363 the twin runs the whole frame on the
 * device, and its conversion, blurs and downsample reproduce the CPU's fp32
 * planes. ssim_map() and edge_diff_map() then form six fp64 terms per sample
 * and add each into one double, pixel after pixel. Since ADR-1446 the twin
 * forms those terms as the reference's doubles, in 64-bit integers
 * (feature/sycl/sycl_ssimulacra2_math.h; a SYCL kernel has no fp64 type,
 * ADR-0220), and returns the bits of the reference's loop over them
 * (feature/sycl/sycl_ordered_sum.h). The score is the CPU's bit for bit, so
 * this test asserts equality where it asserted 5e-3.
 *
 * Before ADR-1446 the terms were pairs of fp32 values added in a fixed
 * tree. On these fixtures that twin's score is 9e-14 to 4e-12 from the
 * CPU's: fifteen of the sixteen score cases below fail on it, at either
 * fixture size, and the identical-frame case passes on both.
 *
 * Cases: 8, 10, 12 and 16 bits; 4:2:0, 4:2:2 and 4:4:4; a frame whose sample
 * count is not a multiple of the sum's chunk (323x181); the smallest frame
 * the twin accepts (8x8, one scale, less than one chunk); 960x540 (all six
 * scales) and 1920x1080 (sums that cross many binades; the size at which a
 * kernel using scratch memory returns wrong values under xe, ADR-1395);
 * each `yuv_matrix`; identical frames (every sum is zero); a frame
 * distorted in its lower third only (each sum starts with a run of zeros);
 * a frame that differs from the reference in a few samples (mostly zero
 * terms). The first case feeds three frames of distinct content: the twin
 * is submit/collect, so frame N is collected after frame N+1 is submitted
 * and a read-back keyed to the wrong frame would score the wrong one. The
 * format, option and content cases use 256x144, and 960x540 in the `_large`
 * variant.
 *
 * Skip behaviour: exits 77 when there is no SYCL device.
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
#include "libvmaf/libvmaf_sycl.h"
#include "libvmaf/picture.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy also proposes the `nullptr` keyword, but this is a C
 * translation unit whose sources spell the null pointer constant `NULL` and
 * MSVC's documented /std:clatest C23 feature set does not include `nullptr`
 * while the required Windows build compiles this TU with cl.exe. ADR-1138. */

/* The frame of the format, option and content cases. The `_large` variant of
 * this test (core/test/meson.build, ADR-1206) builds them at 960x540. */
#ifndef FIXTURE_W
#define FIXTURE_W 256u
#endif
#ifndef FIXTURE_H
#define FIXTURE_H 144u
#endif

enum { MAX_FRAMES = 3 };

/* How the distorted picture differs from the reference. */
enum Distortion {
    DISTORT_ALL = 0,     /* every sample */
    DISTORT_NONE,        /* identical pictures */
    DISTORT_LOWER_THIRD, /* rows from two thirds down */
    DISTORT_FEW,         /* about one sample in a thousand, by one level */
};

typedef struct Case {
    const char *what;
    enum VmafPixelFormat pix_fmt;
    unsigned bpc;
    unsigned w;
    unsigned h;
    unsigned frames;
    enum Distortion distortion;
    const char *yuv_matrix; /* NULL for the default */
} Case;

/* Deterministic position hash. */
static unsigned hash(unsigned row, unsigned col, unsigned salt)
{
    unsigned x = row * 73856093u ^ col * 19349663u ^ salt * 83492791u;
    x ^= x >> 13;
    x *= 0x5bd1e995u;
    x ^= x >> 15;
    return x;
}

/* The error a distorted sample carries, in 8-bit levels. */
static unsigned error_of(const Case *c, unsigned plane, unsigned row, unsigned col, unsigned rows,
                         unsigned frame)
{
    const unsigned salt = 7u + plane + 3u * frame;
    switch (c->distortion) {
    case DISTORT_NONE:
        return 0u;
    case DISTORT_LOWER_THIRD:
        return row >= rows - rows / 3u ? hash(row, col, salt) % 13u : 0u;
    case DISTORT_FEW:
        return hash(row, col, salt) % 997u == 0u ? 1u : 0u;
    default:
        return hash(row, col, salt) % (plane == 0u ? 23u : 7u);
    }
}

/* One sample: 4x4 blocks of structure in every plane (SSIMULACRA 2 reads
 * chroma through the YUV to XYB conversion), low bits below the 8-bit level
 * at higher depths, content that differs from frame to frame. */
static unsigned sample_of(const Case *c, unsigned plane, unsigned row, unsigned col, unsigned rows,
                          unsigned frame, bool distorted)
{
    const unsigned gain = 1u << (c->bpc - 8u);
    const unsigned level = 48u + hash(row >> 2, col >> 2, 1u + plane + 11u * frame) % 160u;
    const unsigned fine = hash(row, col, 5u + plane) % gain;
    const unsigned error = distorted ? error_of(c, plane, row, col, rows, frame) : 0u;
    return (level + error) * gain + fine;
}

static void put_sample(VmafPicture *pic, unsigned plane, unsigned row, unsigned col, unsigned v)
{
    const unsigned peak = (1u << pic->bpc) - 1u;
    uint8_t *line = (uint8_t *)pic->data[plane] + (size_t)row * (size_t)pic->stride[plane];
    if (pic->bpc <= 8u) {
        line[col] = (uint8_t)(v > peak ? peak : v);
    } else {
        ((uint16_t *)line)[col] = (uint16_t)(v > peak ? peak : v);
    }
}

static int fill_picture(VmafPicture *pic, const Case *c, unsigned frame, bool distorted)
{
    const int err = vmaf_picture_alloc(pic, c->pix_fmt, c->bpc, c->w, c->h);
    if (err)
        return err;
    for (unsigned p = 0; p < 3u; p++) {
        for (unsigned row = 0; row < pic->h[p]; row++) {
            for (unsigned col = 0; col < pic->w[p]; col++) {
                put_sample(pic, p, row, col,
                           sample_of(c, p, row, col, pic->h[p], frame, distorted));
            }
        }
    }
    return 0;
}

static int feed_frame(VmafContext *vmaf, const Case *c, unsigned frame)
{
    VmafPicture ref;
    VmafPicture dist;
    int err = fill_picture(&ref, c, frame, false);
    if (err)
        return err;
    err = fill_picture(&dist, c, frame, true);
    if (err) {
        (void)vmaf_picture_unref(&ref);
        return err;
    }
    return vmaf_read_pictures(vmaf, &ref, &dist, frame);
}

/* Feed the case's frames, flush, and read the scores. */
static char *read_scores(VmafContext *vmaf, const Case *c, double score[MAX_FRAMES])
{
    for (unsigned i = 0; i < c->frames; i++)
        mu_assert("feeding a frame failed", !feed_frame(vmaf, c, i));
    mu_assert("vmaf_read_pictures(EOS) failed", !vmaf_read_pictures(vmaf, NULL, NULL, 0));
    for (unsigned i = 0; i < c->frames; i++) {
        mu_assert("vmaf_feature_score_at_index(ssimulacra2) failed",
                  !vmaf_feature_score_at_index(vmaf, "ssimulacra2", &score[i], i));
    }
    return NULL;
}

/* The case's scores on the CPU (`state` NULL) or on the twin. */
static char *scores_of(VmafSyclState *state, const Case *c, double score[MAX_FRAMES])
{
    VmafContext *vmaf = NULL;
    VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    mu_assert("vmaf_init failed", !vmaf_init(&vmaf, cfg));
    char *msg = NULL;
    VmafFeatureDictionary *options = NULL;
    if (state && vmaf_sycl_import_state(vmaf, state))
        msg = "vmaf_sycl_import_state failed";
    if (!msg && c->yuv_matrix && vmaf_feature_dictionary_set(&options, "yuv_matrix", c->yuv_matrix))
        msg = "setting yuv_matrix failed";
    if (!msg && vmaf_use_feature(vmaf, state ? "ssimulacra2_sycl" : "ssimulacra2", options))
        msg = "vmaf_use_feature failed";
    if (!msg)
        msg = read_scores(vmaf, c, score);
    if (vmaf_close(vmaf) != 0 && !msg)
        msg = "vmaf_close failed";
    return msg;
}

/* The twin's score of every frame is the CPU's, bit for bit. A missing
 * device skips the case. `cpu` returns the CPU's scores. */
static char *check_exact(const Case *c, double cpu[MAX_FRAMES])
{
    double gpu[MAX_FRAMES] = {NAN, NAN, NAN};
    mu_assert_msg(scores_of(NULL, c, cpu));
    VmafSyclState *state = NULL;
    VmafSyclConfiguration sycl_cfg = {.device_index = -1};
    if (vmaf_sycl_state_init(&state, sycl_cfg) != 0 || state == NULL) {
        (void)fprintf(stderr, "[skip: no SYCL device] ");
        mu_skipped = 1;
        return NULL;
    }
    char *msg = scores_of(state, c, gpu);
    vmaf_sycl_state_free(&state);
    if (msg)
        return msg;
    for (unsigned i = 0; i < c->frames; i++) {
        mu_assert("the CPU ssimulacra2 score is not finite", isfinite(cpu[i]));
        if (cpu[i] != gpu[i]) {
            (void)fprintf(stderr, "\n%s %ux%u %u-bit frame %u: cpu=%.17g sycl=%.17g delta=%.3e\n",
                          c->what, c->w, c->h, c->bpc, i, cpu[i], gpu[i], fabs(cpu[i] - gpu[i]));
        }
        mu_assert("ssimulacra2_sycl differs from the CPU extractor (ADR-1446)", cpu[i] == gpu[i]);
    }
    return NULL;
}

static char *check_case(const Case *c)
{
    double cpu[MAX_FRAMES] = {0.0};
    return check_exact(c, cpu);
}

static char *test_ssimulacra2_sycl_registered(void)
{
    VmafFeatureExtractor *fex = vmaf_get_feature_extractor_by_name("ssimulacra2_sycl");
    mu_assert("ssimulacra2_sycl extractor must be registered", fex != NULL);
    mu_assert("ssimulacra2_sycl name matches", !strcmp(fex->name, "ssimulacra2_sycl"));
    return NULL;
}

/* ADR-1324 / ADR-1359: inputs the twin's init rejects go to the CPU extractor
 * instead. Boundaries: 8x8 is the smallest accepted frame; 4:0:0 has no
 * chroma to convert. Needs no device. */
static char *test_ssimulacra2_sycl_context_check(void)
{
    VmafFeatureExtractor *fex = vmaf_get_feature_extractor_by_name("ssimulacra2_sycl");
    mu_assert("ssimulacra2_sycl extractor must be registered", fex != NULL);
    mu_assert("ssimulacra2_sycl declares a context check", fex->context_check != NULL);
    mu_assert("ssimulacra2_sycl falls back to the CPU extractor",
              fex->context_fallback_name && !strcmp(fex->context_fallback_name, "ssimulacra2"));
    return NULL;
}

static char *test_ssimulacra2_sycl_context_bounds(void)
{
    VmafFeatureExtractor *fex = vmaf_get_feature_extractor_by_name("ssimulacra2_sycl");
    mu_assert("ssimulacra2_sycl declares a context check", fex && fex->context_check);
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

/* Three frames of distinct content: equal neighbours would hide a read-back
 * keyed to the wrong frame. */
static char *test_ssimulacra2_three_frames(void)
{
    const Case c = {"distorted", VMAF_PIX_FMT_YUV420P, 8u,  FIXTURE_W, FIXTURE_H,
                    3u,          DISTORT_ALL,          NULL};
    double cpu[MAX_FRAMES] = {0.0};
    mu_assert_msg(check_exact(&c, cpu));
    mu_assert("ssimulacra2 fixture frames must score differently",
              mu_skipped || (cpu[0] != cpu[1] && cpu[1] != cpu[2]));
    return NULL;
}

static char *test_ssimulacra2_10bit(void)
{
    const Case c = {"10-bit", VMAF_PIX_FMT_YUV420P, 10u, FIXTURE_W, FIXTURE_H,
                    1u,       DISTORT_ALL,          NULL};
    return check_case(&c);
}

static char *test_ssimulacra2_12bit(void)
{
    const Case c = {"12-bit", VMAF_PIX_FMT_YUV420P, 12u, FIXTURE_W, FIXTURE_H,
                    1u,       DISTORT_ALL,          NULL};
    return check_case(&c);
}

static char *test_ssimulacra2_16bit(void)
{
    const Case c = {"16-bit", VMAF_PIX_FMT_YUV420P, 16u, FIXTURE_W, FIXTURE_H,
                    1u,       DISTORT_ALL,          NULL};
    return check_case(&c);
}

static char *test_ssimulacra2_422_10bit(void)
{
    const Case c = {"4:2:2", VMAF_PIX_FMT_YUV422P, 10u, FIXTURE_W, FIXTURE_H,
                    1u,      DISTORT_ALL,          NULL};
    return check_case(&c);
}

static char *test_ssimulacra2_444(void)
{
    const Case c = {"4:4:4", VMAF_PIX_FMT_YUV444P, 8u, FIXTURE_W, FIXTURE_H, 1u, DISTORT_ALL, NULL};
    return check_case(&c);
}

/* 323 * 181 samples: no scale's count is a multiple of the sum's chunk. */
static char *test_ssimulacra2_odd_frame(void)
{
    const Case c = {"odd", VMAF_PIX_FMT_YUV420P, 8u, 323u, 181u, 1u, DISTORT_ALL, NULL};
    return check_case(&c);
}

/* The smallest accepted frame: one scale of 64 samples, less than a chunk. */
static char *test_ssimulacra2_smallest_frame(void)
{
    const Case c = {"8x8", VMAF_PIX_FMT_YUV420P, 8u, 8u, 8u, 1u, DISTORT_ALL, NULL};
    return check_case(&c);
}

/* All six scales. */
static char *test_ssimulacra2_540p(void)
{
    const Case c = {"540p", VMAF_PIX_FMT_YUV420P, 8u, 960u, 540u, 1u, DISTORT_ALL, NULL};
    return check_case(&c);
}

static char *test_ssimulacra2_1080p(void)
{
    const Case c = {"1080p", VMAF_PIX_FMT_YUV420P, 8u, 1920u, 1080u, 1u, DISTORT_ALL, NULL};
    return check_case(&c);
}

static char *test_ssimulacra2_bt601_limited(void)
{
    const Case c = {
        "yuv_matrix=1", VMAF_PIX_FMT_YUV420P, 8u, FIXTURE_W, FIXTURE_H, 1u, DISTORT_ALL, "1"};
    return check_case(&c);
}

static char *test_ssimulacra2_bt709_full(void)
{
    const Case c = {
        "yuv_matrix=2", VMAF_PIX_FMT_YUV420P, 8u, FIXTURE_W, FIXTURE_H, 1u, DISTORT_ALL, "2"};
    return check_case(&c);
}

static char *test_ssimulacra2_bt601_full(void)
{
    const Case c = {
        "yuv_matrix=3", VMAF_PIX_FMT_YUV420P, 8u, FIXTURE_W, FIXTURE_H, 1u, DISTORT_ALL, "3"};
    return check_case(&c);
}

/* Every term of every sum is zero. */
static char *test_ssimulacra2_identical_frames(void)
{
    const Case c = {"identical", VMAF_PIX_FMT_YUV420P, 8u,  FIXTURE_W, FIXTURE_H,
                    2u,          DISTORT_NONE,         NULL};
    double cpu[MAX_FRAMES] = {0.0};
    mu_assert_msg(check_exact(&c, cpu));
    mu_assert("identical pictures must score 100", mu_skipped || cpu[0] == 100.0);
    return NULL;
}

/* The sums start with a run of zero terms and pick up in the last third. */
static char *test_ssimulacra2_lower_third(void)
{
    const Case c = {"lower third", VMAF_PIX_FMT_YUV420P, 8u, 960u, 540u, 1u, DISTORT_LOWER_THIRD,
                    NULL};
    return check_case(&c);
}

/* Mostly zero terms: chunks of zeros between chunks with a few terms. */
static char *test_ssimulacra2_few_samples(void)
{
    const Case c = {"few samples", VMAF_PIX_FMT_YUV420P, 8u, 960u, 540u, 1u, DISTORT_FEW, NULL};
    return check_case(&c);
}

static char *run_format_cases(void)
{
    mu_run_test(test_ssimulacra2_three_frames);
    mu_run_test(test_ssimulacra2_10bit);
    mu_run_test(test_ssimulacra2_12bit);
    mu_run_test(test_ssimulacra2_16bit);
    mu_run_test(test_ssimulacra2_422_10bit);
    mu_run_test(test_ssimulacra2_444);
    return NULL;
}

static char *run_geometry_cases(void)
{
    mu_run_test(test_ssimulacra2_odd_frame);
    mu_run_test(test_ssimulacra2_smallest_frame);
    mu_run_test(test_ssimulacra2_540p);
    mu_run_test(test_ssimulacra2_1080p);
    return NULL;
}

static char *run_content_and_option_cases(void)
{
    mu_run_test(test_ssimulacra2_bt601_limited);
    mu_run_test(test_ssimulacra2_bt709_full);
    mu_run_test(test_ssimulacra2_bt601_full);
    mu_run_test(test_ssimulacra2_identical_frames);
    mu_run_test(test_ssimulacra2_lower_third);
    mu_run_test(test_ssimulacra2_few_samples);
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_ssimulacra2_sycl_registered);
    mu_run_test(test_ssimulacra2_sycl_context_check);
    mu_run_test(test_ssimulacra2_sycl_context_bounds);
    mu_assert_msg(run_format_cases());
    mu_assert_msg(run_geometry_cases());
    mu_assert_msg(run_content_and_option_cases());
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
