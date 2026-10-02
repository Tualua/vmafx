/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * float_moment CPU vs. a GPU twin: fixtures, the comparison and the cases,
 * for a backend's parity test to instantiate (ADR-1449 for SYCL; the design
 * of ADR-1447).
 *
 * moment.c adds the samples, and their squares, into one double per output.
 * It forms each square in float. Up to 12 bits a square has at most 24
 * significant bits, so the float is the integer square; at 16 bits it is the
 * square rounded to 24 bits. A twin that adds exact integer squares returns
 * the CPU's sum up to 12 bits and not at 16.
 *
 * Natural 16-bit fixtures do not show the difference when they hold 8-bit
 * content shifted left (the squares are then multiples of 2^16 with few
 * significant bits), so the cases here are noise over a range of the bit
 * depth, independent for the reference and the distorted picture. Two frames
 * per case, every output compared with ==.
 *
 * The last case is the range in which the CPU's own sum is no longer exact.
 * Every term is a multiple of 2^-16 below 2^16, so the CPU's running double
 * sum is exact while it is below 2^53 units; it can reach that only on a
 * 16-bit frame of more than 2^21 pixels. From there the CPU rounds every add
 * of a term that is not a multiple of the sum's last place (the squares of
 * samples below 4096), and a twin that holds the exact sum and rounds once
 * may differ from it by at most the bound float_moment_twin_sum_bound()
 * derives. The case is a 2560x1440 frame, nine tenths of it near the peak
 * and one tenth below 4096 (sum about 1.5 * 2^53). It asserts the bound,
 * that the frame really is past 2^53, and that the CPU's sum did round
 * there, so the case cannot pass by not reaching the range.
 *
 * A test includes this header once, after defining FIXTURE_W / FIXTURE_H if
 * it wants another size for the noise cases, and provides a
 * FloatMomentTwin. A missing device skips the case and the run exits 77.
 */

#ifndef LIBVMAF_TEST_FLOAT_MOMENT_TWIN_PARITY_H_
#define LIBVMAF_TEST_FLOAT_MOMENT_TWIN_PARITY_H_

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test.h"

#include "feature/feature_extractor.h"
#include "libvmaf/libvmaf.h"
#include "libvmaf/picture.h"

/* NOLINTBEGIN(modernize-use-nullptr): this is a C23 translation unit, but the
 * required MSVC C lane does not provide the C nullptr spelling clang-tidy
 * proposes. Keep the portable C API form under ADR-1138. */

#ifndef FIXTURE_W
#define FIXTURE_W 256u
#endif
#ifndef FIXTURE_H
#define FIXTURE_H 144u
#endif

enum {
    FLOAT_MOMENT_TWIN_FRAMES = 2,
    FLOAT_MOMENT_TWIN_OUTPUTS = 4,
    FLOAT_MOMENT_TWIN_FIRST_SECOND = 2,
    FLOAT_MOMENT_TWIN_DARK_PEAK = 4095,
};

static const char *const FLOAT_MOMENT_TWIN_FEATURES[FLOAT_MOMENT_TWIN_OUTPUTS] = {
    "float_moment_ref1st",
    "float_moment_dis1st",
    "float_moment_ref2nd",
    "float_moment_dis2nd",
};

/* The backend under test. */
typedef struct FloatMomentTwin {
    const char *extractor; /* "float_moment_sycl", ... */
    const char *backend;   /* for messages */
    /* Opens a device state. Non-zero: no device, the case is skipped. */
    int (*open)(void **state);
    int (*import)(VmafContext *vmaf, void *state);
    int (*close)(void *state);
} FloatMomentTwin;

/* A frame of noise: luma samples uniform in [lo, hi], and `dark_percent` of
 * them uniform in [1, FLOAT_MOMENT_TWIN_DARK_PEAK] instead. */
typedef struct FloatMomentTwinCase {
    const char *name;
    unsigned w;
    unsigned h;
    unsigned bpc;
    unsigned lo;
    unsigned hi;
    unsigned dark_percent;
} FloatMomentTwinCase;

typedef struct FloatMomentTwinScores {
    double v[FLOAT_MOMENT_TWIN_FRAMES][FLOAT_MOMENT_TWIN_OUTPUTS];
} FloatMomentTwinScores;

/* lowbias32 hash of the position, the frame and the picture: stateless, so
 * both runs see the same pictures. */
static inline uint32_t float_moment_twin_hash(unsigned row, unsigned col, unsigned salt)
{
    uint32_t x = ((uint32_t)row << 16) ^ (uint32_t)col ^ (salt * 0x9E3779B9u);
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static inline void float_moment_twin_put(VmafPicture *pic, unsigned plane, unsigned row,
                                         unsigned col, unsigned v)
{
    uint8_t *line = (uint8_t *)pic->data[plane] + ((size_t)row * (size_t)pic->stride[plane]);
    if (pic->bpc <= 8u) {
        line[col] = (uint8_t)v;
    } else {
        ((uint16_t *)line)[col] = (uint16_t)v;
    }
}

/* Noise in the case's range on luma, mid-grey on chroma (the moments are luma
 * only); `salt` separates the frames and the two pictures of a frame. */
static inline int float_moment_twin_fill(VmafPicture *pic, const FloatMomentTwinCase *c,
                                         unsigned salt)
{
    const int err = vmaf_picture_alloc(pic, VMAF_PIX_FMT_YUV420P, c->bpc, c->w, c->h);
    if (err) {
        return err;
    }
    const unsigned span = c->hi - c->lo + 1u;
    for (unsigned row = 0; row < pic->h[0]; row++) {
        for (unsigned col = 0; col < pic->w[0]; col++) {
            const uint32_t hash = float_moment_twin_hash(row, col, salt);
            const bool dark =
                (float_moment_twin_hash(col, row, salt + 3u) % 100u) < c->dark_percent;
            const unsigned v =
                dark ? 1u + (hash % (unsigned)FLOAT_MOMENT_TWIN_DARK_PEAK) : c->lo + (hash % span);
            float_moment_twin_put(pic, 0u, row, col, v);
        }
    }
    for (unsigned p = 1; p < 3u; p++) {
        for (unsigned row = 0; row < pic->h[p]; row++) {
            for (unsigned col = 0; col < pic->w[p]; col++) {
                float_moment_twin_put(pic, p, row, col, 1u << (c->bpc - 1u));
            }
        }
    }
    return 0;
}

/* Frame `frame` of the case through `vmaf`, which takes both pictures. */
static inline int float_moment_twin_feed(VmafContext *vmaf, const FloatMomentTwinCase *c,
                                         unsigned frame)
{
    VmafPicture ref;
    VmafPicture dist;
    int err = float_moment_twin_fill(&ref, c, (frame * 16u) + 1u);
    if (err) {
        return err;
    }
    err = float_moment_twin_fill(&dist, c, (frame * 16u) + 8u);
    if (err) {
        (void)vmaf_picture_unref(&ref);
        return err;
    }
    return vmaf_read_pictures(vmaf, &ref, &dist, frame);
}

/* The case's frames through the CPU `float_moment` (`state` NULL) or the
 * twin, and every output of every frame read into `out`. Returns the first
 * error. */
static inline int float_moment_twin_scores(const FloatMomentTwin *twin, void *state,
                                           const FloatMomentTwinCase *c, FloatMomentTwinScores *out)
{
    const VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    VmafContext *vmaf = NULL;
    int err = vmaf_init(&vmaf, cfg);
    if (!err && state) {
        err = twin->import(vmaf, state);
    }
    if (!err) {
        err = vmaf_use_feature(vmaf, state ? twin->extractor : "float_moment", NULL);
    }
    for (unsigned frame = 0; frame < FLOAT_MOMENT_TWIN_FRAMES && !err; frame++) {
        err = float_moment_twin_feed(vmaf, c, frame);
    }
    if (!err) {
        err = vmaf_read_pictures(vmaf, NULL, NULL, 0);
    }
    for (unsigned i = 0; i < FLOAT_MOMENT_TWIN_FRAMES * FLOAT_MOMENT_TWIN_OUTPUTS && !err; i++) {
        const unsigned frame = i / FLOAT_MOMENT_TWIN_OUTPUTS;
        const unsigned m = i % FLOAT_MOMENT_TWIN_OUTPUTS;
        err = vmaf_feature_score_at_index(vmaf, FLOAT_MOMENT_TWIN_FEATURES[m], &out->v[frame][m],
                                          frame);
    }
    const int closed = vmaf ? vmaf_close(vmaf) : 0;
    return err ? err : closed;
}

/* The CPU's and the twin's outputs for one case. Returns 0, a negative errno
 * when a run failed, or 1 when the twin's leg was skipped (no device). */
static inline int float_moment_twin_run(const FloatMomentTwin *twin, const FloatMomentTwinCase *c,
                                        FloatMomentTwinScores *cpu, FloatMomentTwinScores *gpu)
{
    void *state = NULL;
    if (twin->open(&state) != 0 || state == NULL) {
        (void)fprintf(stderr, "[skip: no %s device] ", twin->backend);
        mu_skipped = 1;
        return 1;
    }
    const int gpu_err = float_moment_twin_scores(twin, state, c, gpu);
    const int close_err = twin->close(state);
    const int cpu_err = gpu_err ? 0 : float_moment_twin_scores(twin, NULL, c, cpu);
    if (gpu_err || cpu_err || close_err) {
        (void)fprintf(stderr, "\n%s: run failed (%s %d, cpu %d, close %d)\n", c->name,
                      twin->backend, gpu_err, cpu_err, close_err);
        return gpu_err ? gpu_err : (cpu_err ? cpu_err : -EIO);
    }
    return 0;
}

/* Outputs of the case that are not the CPU's, each one reported; UINT32_MAX
 * when a run failed. A skipped leg counts as 0. */
static inline unsigned float_moment_twin_mismatches(const FloatMomentTwin *twin,
                                                    const FloatMomentTwinCase *c)
{
    static FloatMomentTwinScores cpu;
    static FloatMomentTwinScores gpu;
    const int run = float_moment_twin_run(twin, c, &cpu, &gpu);
    if (run != 0) {
        return run > 0 ? 0u : UINT32_MAX;
    }
    unsigned mismatches = 0u;
    for (unsigned i = 0; i < FLOAT_MOMENT_TWIN_FRAMES * FLOAT_MOMENT_TWIN_OUTPUTS; i++) {
        const unsigned frame = i / FLOAT_MOMENT_TWIN_OUTPUTS;
        const unsigned m = i % FLOAT_MOMENT_TWIN_OUTPUTS;
        const double a = cpu.v[frame][m];
        const double b = gpu.v[frame][m];
        if (isfinite(a) && a == b) {
            continue;
        }
        mismatches++;
        (void)fprintf(stderr, "\n%s frame %u %s: cpu=%.17g %s=%.17g delta=%.3e", c->name, frame,
                      FLOAT_MOMENT_TWIN_FEATURES[m], a, twin->backend, b, fabs(a - b));
    }
    if (mismatches != 0u) {
        (void)fprintf(stderr, "\n");
    }
    return mismatches;
}

/* Largest distance between the CPU's second moment and the twin's on a 16-bit
 * frame of `pixels` pixels whose moment is `moment`, once the sum of the
 * float squares has reached 2^53 units of 2^-16.
 *
 * A term is below 2^32 units, so at least 2^21 adds are exact before the sum
 * can reach 2^53; each later add of the CPU rounds by at most half a unit in
 * the last place of the sum, 2^(e - 53) units for a sum in binade e. The twin
 * rounds the exact sum once, by at most the same amount. In moment units
 * (2^-16 per unit, divided by the pixel count) that is
 * (pixels - 2^21 + 1) / pixels * 2^(e - 69), plus 2^-37 for the two final
 * divisions, each within half a unit in the last place of a value below
 * 2^16. */
static inline double float_moment_twin_sum_bound(double pixels, double moment)
{
    const double units = moment * pixels * 65536.0;
    int binade = 53;
    while (ldexp(1.0, binade + 1) <= units * (1.0 + 0x1p-40)) {
        binade++;
    }
    const double rounding_adds = pixels - 0x1p21 + 1.0;
    return rounding_adds / pixels * ldexp(1.0, binade - 69) + 0x1p-37;
}

static inline mu_message_t float_moment_twin_registered(const FloatMomentTwin *twin)
{
    VmafFeatureExtractor *fex = vmaf_get_feature_extractor_by_name(twin->extractor);
    mu_assert("the float_moment twin must be registered", fex != NULL);
    mu_assert("the float_moment twin's name matches", !strcmp(fex->name, twin->extractor));
    return NULL;
}

/* Full-range noise at `bpc` bits, every output equal to the CPU's. */
static inline mu_message_t float_moment_twin_noise_exact(const FloatMomentTwin *twin, unsigned bpc)
{
    const FloatMomentTwinCase c = {"noise", FIXTURE_W, FIXTURE_H, bpc, 0u, (1u << bpc) - 1u, 0u};
    mu_assert("the float_moment twin is not bit-identical to the CPU on noise",
              float_moment_twin_mismatches(twin, &c) == 0u);
    return NULL;
}

/* 1920x1080 is below 2^21 pixels: the sum stays below 2^53 whatever the
 * content, so samples near the peak are exact too. */
static inline mu_message_t float_moment_twin_bright_1080p_exact(const FloatMomentTwin *twin)
{
    const FloatMomentTwinCase c = {
        "16-bit bright 1920x1080", 1920u, 1080u, 16u, 56000u, 65535u, 10u};
    mu_assert(
        "the float_moment twin is not bit-identical to the CPU on a bright 16-bit 1080p frame",
        float_moment_twin_mismatches(twin, &c) == 0u);
    return NULL;
}

/* One second moment past 2^53 against the CPU's: inside the derived bound, and
 * the frame past 2^53 units. Returns the distance, or a negative value when
 * either does not hold (reported). */
static inline double float_moment_twin_past_distance(const FloatMomentTwinCase *c,
                                                     const char *feature, double cpu, double gpu)
{
    const double pixels = (double)c->w * (double)c->h;
    const double bound = float_moment_twin_sum_bound(pixels, cpu);
    const double delta = fabs(cpu - gpu);
    if (cpu * pixels >= 0x1p37 && delta <= bound) {
        return delta;
    }
    (void)fprintf(stderr, "\n%s %s: cpu=%.17g twin=%.17g delta=%.3e bound=%.3e units=2^%.3f\n",
                  c->name, feature, cpu, gpu, delta, bound, log2(cpu * pixels * 65536.0));
    return -1.0;
}

/* Past 2^53: the first moments stay exact, the second moments are within the
 * derived bound of the CPU's sequentially rounded sum, and that sum did
 * round. */
static inline mu_message_t float_moment_twin_past_2_53_within_bound(const FloatMomentTwin *twin)
{
    const FloatMomentTwinCase c = {
        "16-bit 2560x1440 past 2^53", 2560u, 1440u, 16u, 64000u, 65535u, 10u};
    static FloatMomentTwinScores cpu;
    static FloatMomentTwinScores gpu;
    const int run = float_moment_twin_run(twin, &c, &cpu, &gpu);
    mu_assert("the runs past 2^53 failed", run >= 0);
    if (run > 0) {
        return NULL;
    }
    double largest = 0.0;
    for (unsigned i = 0; i < FLOAT_MOMENT_TWIN_FRAMES * FLOAT_MOMENT_TWIN_OUTPUTS; i++) {
        const unsigned m = i % FLOAT_MOMENT_TWIN_OUTPUTS;
        const double a = cpu.v[i / FLOAT_MOMENT_TWIN_OUTPUTS][m];
        const double b = gpu.v[i / FLOAT_MOMENT_TWIN_OUTPUTS][m];
        if (m < FLOAT_MOMENT_TWIN_FIRST_SECOND) {
            mu_assert("a first moment past 2^53 is not the CPU's", isfinite(a) && a == b);
            continue;
        }
        const double delta =
            float_moment_twin_past_distance(&c, FLOAT_MOMENT_TWIN_FEATURES[m], a, b);
        mu_assert("a second moment past 2^53 is outside the derived bound", delta >= 0.0);
        largest = delta > largest ? delta : largest;
    }
    (void)fprintf(stderr, "[past 2^53: largest distance %.3e] ", largest);
    mu_assert("the CPU's sum did not round past 2^53: the case no longer reaches that range",
              largest > 0.0);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */

#endif /* LIBVMAF_TEST_FLOAT_MOMENT_TWIN_PARITY_H_ */
