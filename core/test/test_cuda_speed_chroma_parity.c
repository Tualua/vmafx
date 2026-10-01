/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * speed_chroma CPU vs. CUDA parity test (ADR-0965; bound from ADR-1430).
 *
 * Asserts that the three scores of the CPU extractor `speed_chroma` and of
 * the CUDA twin `speed_chroma_cuda` agree at every frame to one part in a
 * million.
 *
 * The twin runs speed.c's arithmetic and rounds log2 correctly (ADR-1380);
 * speed.c calls the C library's `log2f`. With a correctly rounded `log2f`
 * preloaded the two return the same bits, on this fixture too. glibc's is
 * the neighbouring float for 0.015 % to 0.97 % of the arguments of a binade,
 * which moves some scores by a few steps of the fp32 result: at most five
 * steps and 3.9e-7 of the score on the video fixtures, two steps (3.8e-6 at a
 * score of 22.5, 1.7e-7 of it) on this one with glibc 2.44. So this is a
 * bound and not an equality. It is relative because the scores are floats:
 * the cross-backend gate's absolute 5e-6 (`LIBM_TWINS`) is for its fixtures,
 * whose scores stay below 16. The test allowed 1e-4 on one score of one
 * frame before the cause was measured.
 *
 * Skip behaviour: if `vmaf_cuda_state_init()` fails (no CUDA driver or no
 * device visible) the test emits `[skip: no CUDA device]` and exits 77, meson's
 * "skipped" status.
 * Mirrors the pattern from test_cuda_motion3_parity.c.
 */

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test.h"

#include "libvmaf/libvmaf.h"
#include "libvmaf/libvmaf_cuda.h"
#include "libvmaf/picture.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy also proposes the `nullptr` keyword, but MSVC's
 * documented /std:clatest C23 feature set does not include `nullptr` while the
 * required Windows build compiles this TU with cl.exe, and this test mirrors
 * the C spelling of the surface it exercises. ADR-1138. */

/* Fixture geometry. SpEED estimates a 25x25 covariance from one 25-vector
 * per 5x5 block of the 16x downscaled plane; 960x960 gives chroma 480x480 ->
 * 30x30 -> 36 blocks, enough for a regular covariance on a textured frame.
 * The 768x432 fixture this test had gave 8 blocks, a covariance that is
 * singular on every frame, so the scoring path with its log2f calls never
 * ran (see test_cuda_speed_singular_parity.c). */
#ifndef FIXTURE_W
#define FIXTURE_W 960u
#endif
#ifndef FIXTURE_H
#define FIXTURE_H 960u
#endif
#define FIXTURE_BPC 8u
#define NUM_FRAMES 2u

/* |cpu - cuda| <= PARITY_REL * |cpu| (ADR-1430). */
#define PARITY_REL 1e-6

#define NUM_SCORES 3u

static const char *const score_keys[NUM_SCORES] = {
    "Speed_chroma_feature_speed_chroma_u_score",
    "Speed_chroma_feature_speed_chroma_v_score",
    "Speed_chroma_feature_speed_chroma_uv_score",
};

/* Every score of every frame, [score][frame]. */
typedef struct Scores {
    double v[NUM_SCORES][NUM_FRAMES];
} Scores;

static void scores_clear(Scores *scores)
{
    for (unsigned k = 0; k < NUM_SCORES; k++) {
        for (unsigned i = 0; i < NUM_FRAMES; i++)
            scores->v[k][i] = NAN;
    }
}

static char *read_scores(VmafContext *vmaf, Scores *scores)
{
    for (unsigned k = 0; k < NUM_SCORES; k++) {
        for (unsigned i = 0; i < NUM_FRAMES; i++) {
            const int err = vmaf_feature_score_at_index(vmaf, score_keys[k], &scores->v[k][i], i);
            mu_assert("vmaf_feature_score_at_index(speed_chroma) failed", !err);
        }
    }
    return NULL;
}

/* Deterministic texture. A ramp is too structured: its 25-vectors span a
 * low-dimensional subspace and the covariance stays singular. */
static unsigned splatter(unsigned row, unsigned col, unsigned idx, unsigned plane)
{
    unsigned x = row * 73856093u ^ col * 19349663u ^ idx * 83492791u ^ plane * 2654435761u;
    x ^= x >> 15;
    x *= 2246822519u;
    x ^= x >> 13;
    x *= 3266489917u;
    x ^= x >> 16;
    return x & 0xFFu;
}

static int fill_fixture(VmafPicture *pic, unsigned frame_idx, int distort)
{
    int err = vmaf_picture_alloc(pic, VMAF_PIX_FMT_YUV420P, FIXTURE_BPC, FIXTURE_W, FIXTURE_H);
    if (err)
        return err;
    for (unsigned p = 0; p < 3; p++) {
        uint8_t *plane = (uint8_t *)pic->data[p];
        for (unsigned row = 0; row < pic->h[p]; row++) {
            for (unsigned col = 0; col < pic->w[p]; col++) {
                const unsigned d = (distort && p > 0u) ? ((row + col) & 0x07u) : 0u;
                plane[row * pic->stride[p] + col] =
                    (uint8_t)((splatter(row, col, frame_idx, p) + d) & 0xFFu);
            }
        }
    }
    return 0;
}

static char *feed_chroma_frames(VmafContext *vmaf)
{
    for (unsigned i = 0; i < NUM_FRAMES; i++) {
        VmafPicture ref;
        VmafPicture dist;
        int err = fill_fixture(&ref, i, 0);
        mu_assert("fill_fixture(ref) failed", !err);
        err = fill_fixture(&dist, i, 1);
        mu_assert("fill_fixture(dist) failed", !err);
        err = vmaf_read_pictures(vmaf, &ref, &dist, i);
        mu_assert("vmaf_read_pictures failed", !err);
    }
    int err = vmaf_read_pictures(vmaf, NULL, NULL, 0);
    mu_assert("vmaf_read_pictures(EOS) failed", !err);
    return NULL;
}

static char *run_cpu(Scores *out_scores)
{
    scores_clear(out_scores);
    VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    VmafContext *vmaf = NULL;
    int err = vmaf_init(&vmaf, cfg);
    mu_assert("CPU: vmaf_init failed", !err);

    err = vmaf_use_feature(vmaf, "speed_chroma", NULL);
    mu_assert("CPU: vmaf_use_feature(speed_chroma) failed", !err);

    mu_assert_msg(feed_chroma_frames(vmaf));
    mu_assert_msg(read_scores(vmaf, out_scores));

    err = vmaf_close(vmaf);
    mu_assert("CPU: vmaf_close failed", !err);
    return NULL;
}

static char *setup_cuda_speed_chroma_context(VmafContext **out_vmaf, VmafCudaState **out_cu_state)
{
    *out_vmaf = NULL;
    *out_cu_state = NULL;

    VmafCudaState *cu_state = NULL;
    VmafCudaConfiguration cuda_cfg = {0};
    int err = vmaf_cuda_state_init(&cu_state, cuda_cfg);
    if (err != 0 || cu_state == NULL) {
        (void)fprintf(stderr, "[skip: no CUDA device] ");
        mu_skipped = 1; /* exit 77, not a pass */
        return NULL;
    }

    VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    VmafContext *vmaf = NULL;
    err = vmaf_init(&vmaf, cfg);
    mu_assert("CUDA: vmaf_init failed", !err);

    err = vmaf_cuda_import_state(vmaf, cu_state);
    mu_assert("CUDA: vmaf_cuda_import_state failed", !err);

    err = vmaf_use_feature(vmaf, "speed_chroma_cuda", NULL);
    mu_assert("CUDA: vmaf_use_feature(speed_chroma_cuda) failed", !err);

    *out_vmaf = vmaf;
    *out_cu_state = cu_state;
    return NULL;
}

static char *run_cuda(Scores *out_scores)
{
    scores_clear(out_scores);

    VmafContext *vmaf = NULL;
    VmafCudaState *cu_state = NULL;
    mu_assert_msg(setup_cuda_speed_chroma_context(&vmaf, &cu_state));
    if (!vmaf)
        return NULL;

    mu_assert_msg(feed_chroma_frames(vmaf));
    mu_assert_msg(read_scores(vmaf, out_scores));

    int err = vmaf_close(vmaf);
    mu_assert("CUDA: vmaf_close failed", !err);

    err = vmaf_cuda_state_free(cu_state);
    mu_assert("CUDA: vmaf_cuda_state_free failed", !err);
    return NULL;
}

/* Scores further apart than PARITY_REL of the CPU's, over every score and
 * frame. A NaN on either side counts. */
static unsigned scores_outside_bound(const Scores *cpu, const Scores *cuda)
{
    unsigned outside = 0;
    for (unsigned k = 0; k < NUM_SCORES; k++) {
        for (unsigned i = 0; i < NUM_FRAMES; i++) {
            const double delta = fabs(cpu->v[k][i] - cuda->v[k][i]);
            const double bound = PARITY_REL * fabs(cpu->v[k][i]);
            if (delta <= bound)
                continue;
            (void)fprintf(stderr, "\n%s frame %u: cpu=%.9g cuda=%.9g delta=%.3e bound=%.3e\n",
                          score_keys[k], i, cpu->v[k][i], cuda->v[k][i], delta, bound);
            outside++;
        }
    }
    return outside;
}

static char *test_speed_chroma_cpu_cuda_parity(void)
{
    Scores cpu;
    Scores cuda;

    char *msg = run_cpu(&cpu);
    if (msg)
        return msg;

    msg = run_cuda(&cuda);
    if (msg)
        return msg;

    if (mu_skipped)
        return NULL;

    mu_assert("speed_chroma_cuda is further from the CPU extractor than its log2f explains",
              scores_outside_bound(&cpu, &cuda) == 0u);
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_speed_chroma_cpu_cuda_parity);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
