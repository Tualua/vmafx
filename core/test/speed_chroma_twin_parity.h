/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * speed_chroma CPU vs. GPU twin: the fixture, the CPU run and the comparison
 * a twin's parity test wraps (ADR-1430).
 *
 * A twin that runs speed.c's arithmetic and rounds log2 correctly differs
 * from the CPU extractor only through the C library's `log2f`, which speed.c
 * calls. With a correctly rounded `log2f` preloaded the two return the same
 * bits, on this fixture too. glibc's is the neighbouring float for 0.015 % to
 * 0.97 % of the arguments of a binade, which moves some scores by a few steps
 * of the fp32 result: at most five steps and 3.9e-7 of the score on the video
 * fixtures, two steps (3.8e-6 at a score of 22.5, 1.7e-7 of it) on this one
 * with glibc 2.44. So the comparison is a bound and not an equality. It is
 * relative because the scores are floats: the cross-backend gate's absolute
 * 5e-6 (`LIBM_TWINS`) is for its fixtures, whose scores stay below 16.
 *
 * A test opens its backend's device state and context, registers the twin,
 * then calls speed_chroma_twin_feed(), speed_chroma_twin_read() and
 * speed_chroma_twin_outside_bound().
 */

#ifndef LIBVMAF_TEST_SPEED_CHROMA_TWIN_PARITY_H_
#define LIBVMAF_TEST_SPEED_CHROMA_TWIN_PARITY_H_

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "test.h"

#include "libvmaf/libvmaf.h"
#include "libvmaf/picture.h"

/* NOLINTBEGIN(modernize-use-nullptr): included by C translation units. The
 * fork builds C as C23, where clang-tidy proposes `nullptr`, but the required
 * MSVC C build does not provide that keyword. Preserve the portable C
 * spelling. ADR-1138. */

/* Fixture geometry. SpEED estimates a 25x25 covariance from one 25-vector
 * per 5x5 block of the 16x downscaled plane; 960x960 gives chroma 480x480 ->
 * 30x30 -> 36 blocks, enough for a regular covariance on a textured frame.
 * A 768x432 fixture gives 8 blocks, a covariance that is singular on every
 * frame, so the scoring path with its log2f calls never runs (see
 * test_cuda_speed_singular_parity.c and test_hip_speed_singular_parity.c). */
#ifndef FIXTURE_W
#define FIXTURE_W 960u
#endif
#ifndef FIXTURE_H
#define FIXTURE_H 960u
#endif
#define SPEED_CHROMA_TWIN_BPC 8u
#define SPEED_CHROMA_TWIN_FRAMES 2u

/* |cpu - twin| <= SPEED_CHROMA_TWIN_REL * |cpu| (ADR-1430). */
#define SPEED_CHROMA_TWIN_REL 1e-6

#define SPEED_CHROMA_TWIN_SCORES 3u

static const char *const speed_chroma_twin_keys[SPEED_CHROMA_TWIN_SCORES] = {
    "Speed_chroma_feature_speed_chroma_u_score",
    "Speed_chroma_feature_speed_chroma_v_score",
    "Speed_chroma_feature_speed_chroma_uv_score",
};

/* Every score of every frame, [score][frame]. */
typedef struct SpeedChromaTwinScores {
    double v[SPEED_CHROMA_TWIN_SCORES][SPEED_CHROMA_TWIN_FRAMES];
} SpeedChromaTwinScores;

static inline void speed_chroma_twin_clear(SpeedChromaTwinScores *scores)
{
    for (unsigned k = 0; k < SPEED_CHROMA_TWIN_SCORES; k++) {
        for (unsigned i = 0; i < SPEED_CHROMA_TWIN_FRAMES; i++)
            scores->v[k][i] = NAN;
    }
}

static inline mu_message_t speed_chroma_twin_read(VmafContext *vmaf, SpeedChromaTwinScores *scores)
{
    for (unsigned k = 0; k < SPEED_CHROMA_TWIN_SCORES; k++) {
        for (unsigned i = 0; i < SPEED_CHROMA_TWIN_FRAMES; i++) {
            const int err =
                vmaf_feature_score_at_index(vmaf, speed_chroma_twin_keys[k], &scores->v[k][i], i);
            mu_assert("vmaf_feature_score_at_index(speed_chroma) failed", !err);
        }
    }
    return NULL;
}

/* Deterministic texture. A ramp is too structured: its 25-vectors span a
 * low-dimensional subspace and the covariance stays singular. */
static inline unsigned speed_chroma_twin_splatter(unsigned row, unsigned col, unsigned idx,
                                                  unsigned plane)
{
    unsigned x = row * 73856093u ^ col * 19349663u ^ idx * 83492791u ^ plane * 2654435761u;
    x ^= x >> 15;
    x *= 2246822519u;
    x ^= x >> 13;
    x *= 3266489917u;
    x ^= x >> 16;
    return x & 0xFFu;
}

static inline int speed_chroma_twin_fill(VmafPicture *pic, unsigned frame_idx, int distort)
{
    int err =
        vmaf_picture_alloc(pic, VMAF_PIX_FMT_YUV420P, SPEED_CHROMA_TWIN_BPC, FIXTURE_W, FIXTURE_H);
    if (err)
        return err;
    for (unsigned p = 0; p < 3; p++) {
        uint8_t *plane = (uint8_t *)pic->data[p];
        for (unsigned row = 0; row < pic->h[p]; row++) {
            for (unsigned col = 0; col < pic->w[p]; col++) {
                const unsigned d = (distort && p > 0u) ? ((row + col) & 0x07u) : 0u;
                plane[row * pic->stride[p] + col] =
                    (uint8_t)((speed_chroma_twin_splatter(row, col, frame_idx, p) + d) & 0xFFu);
            }
        }
    }
    return 0;
}

/* Feed the frames and flush. Returns the first error, so a caller can tell a
 * backend's scaffold (-ENOSYS) from a failure. */
static inline int speed_chroma_twin_feed(VmafContext *vmaf)
{
    for (unsigned i = 0; i < SPEED_CHROMA_TWIN_FRAMES; i++) {
        VmafPicture ref;
        VmafPicture dist;
        int err = speed_chroma_twin_fill(&ref, i, 0);
        if (err)
            return err;
        err = speed_chroma_twin_fill(&dist, i, 1);
        if (err) {
            (void)vmaf_picture_unref(&ref);
            return err;
        }
        err = vmaf_read_pictures(vmaf, &ref, &dist, i);
        if (err)
            return err;
    }
    return vmaf_read_pictures(vmaf, NULL, NULL, 0);
}

static inline mu_message_t speed_chroma_twin_run_cpu(SpeedChromaTwinScores *out_scores)
{
    speed_chroma_twin_clear(out_scores);
    VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    VmafContext *vmaf = NULL;
    int err = vmaf_init(&vmaf, cfg);
    mu_assert("CPU: vmaf_init failed", !err);

    err = vmaf_use_feature(vmaf, "speed_chroma", NULL);
    mu_assert("CPU: vmaf_use_feature(speed_chroma) failed", !err);

    err = speed_chroma_twin_feed(vmaf);
    mu_assert("CPU: feeding the frames failed", !err);
    mu_assert_msg(speed_chroma_twin_read(vmaf, out_scores));

    err = vmaf_close(vmaf);
    mu_assert("CPU: vmaf_close failed", !err);
    return NULL;
}

/* Scores further apart than SPEED_CHROMA_TWIN_REL of the CPU's, over every
 * score and frame. A NaN on either side counts. */
static inline unsigned speed_chroma_twin_outside_bound(const SpeedChromaTwinScores *cpu,
                                                       const SpeedChromaTwinScores *twin,
                                                       const char *backend)
{
    unsigned outside = 0;
    for (unsigned k = 0; k < SPEED_CHROMA_TWIN_SCORES; k++) {
        for (unsigned i = 0; i < SPEED_CHROMA_TWIN_FRAMES; i++) {
            const double delta = fabs(cpu->v[k][i] - twin->v[k][i]);
            const double bound = SPEED_CHROMA_TWIN_REL * fabs(cpu->v[k][i]);
            if (delta <= bound)
                continue;
            (void)fprintf(stderr, "\n%s frame %u: cpu=%.9g %s=%.9g delta=%.3e bound=%.3e\n",
                          speed_chroma_twin_keys[k], i, cpu->v[k][i], backend, twin->v[k][i], delta,
                          bound);
            outside++;
        }
    }
    return outside;
}

/* NOLINTEND(modernize-use-nullptr) */

#endif /* LIBVMAF_TEST_SPEED_CHROMA_TWIN_PARITY_H_ */
