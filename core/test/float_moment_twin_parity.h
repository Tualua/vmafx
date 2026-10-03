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
 * The cases past 2^53 are the range in which the CPU's own sum is no longer
 * exact. Every term is a multiple of 2^-16 below 2^16, so the CPU's running
 * double sum is exact while it is below 2^53 units; it can reach that only on
 * a 16-bit frame of more than 2^21 pixels. From there the CPU rounds every add
 * of a term that is not a multiple of the sum's last place, and a twin must
 * round where it rounds: the twins form the CPU's sum with
 * feature/float_moment_sum.h (ADR-1497). The cases are a 2560x1440 frame nine
 * tenths of it near the peak and one tenth below 8192 (sum about 1.5 * 2^53),
 * a 4096x2560 frame of the same kind that crosses 2^53, 2^54 and 2^55, and
 * three frames whose exact sum of squares is 2^53 - 1, 2^53, and 2^53
 * followed by three terms of 1 that the CPU's sum drops (a tie at 2^53 goes
 * to the even value). The dark samples give terms that tie in the last place
 * at 2^53 (odd squares) and at 2^55 (squares of twice an odd number below
 * 2048, 4 more than a multiple of 8; no float square ties at 2^54), and past
 * 2^55 increments of both parities, so a sum that starts a row at an odd
 * multiple of its last place meets ties as well. Each case compares
 * every output with ==, and checks on the host that the frame reaches the
 * range it is meant for and, where the CPU rounds, that the exact sum is
 * another number, so a twin that returns the exact sum fails.
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
    FLOAT_MOMENT_TWIN_DARK_PEAK = 8191,
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
 * them uniform in [1, FLOAT_MOMENT_TWIN_DARK_PEAK] instead. A case with a
 * `target` is a frame whose float squares add up to exactly `target` units of
 * 1 / scaler^2, followed by `ones` samples of 1 (float_moment_twin_fill_sum()).
 * `rounds` says that the CPU's sum of squares rounds on the frame, so that it
 * differs from the exact sum. */
typedef struct FloatMomentTwinCase {
    const char *name;
    unsigned w;
    unsigned h;
    unsigned bpc;
    unsigned lo;
    unsigned hi;
    unsigned dark_percent;
    uint64_t target;
    unsigned ones;
    bool rounds;
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

/* The CPU's term for a raw sample `v`, in units of 1 / scaler^2: the float
 * square moment.c forms (one fp32 product), an integer below 2^32. */
static inline uint32_t float_moment_twin_float_square(unsigned v)
{
    const float sample = (float)v;
    const float square = sample * sample;
    return (uint32_t)square;
}

/* The largest 16-bit sample whose float square is at most `rest`. */
static inline unsigned float_moment_twin_largest_fitting(uint64_t rest)
{
    unsigned lo = 0u;
    unsigned hi = 65535u;
    for (unsigned i = 0; i < 17u && lo < hi; i++) {
        const unsigned mid = (lo + hi + 1u) / 2u;
        if ((uint64_t)float_moment_twin_float_square(mid) <= rest) {
            lo = mid;
        } else {
            hi = mid - 1u;
        }
    }
    return lo;
}

/* Luma of a case with a target: in raster order, the largest samples whose
 * float squares still fit what is left of `target` until it is reached
 * exactly, then `ones` samples of 1, then zeros. */
static inline void float_moment_twin_fill_sum(VmafPicture *pic, const FloatMomentTwinCase *c)
{
    uint64_t rest = c->target;
    unsigned ones = c->ones;
    for (unsigned row = 0; row < pic->h[0]; row++) {
        for (unsigned col = 0; col < pic->w[0]; col++) {
            unsigned v = 0u;
            if (rest > 0u) {
                v = float_moment_twin_largest_fitting(rest);
                rest -= float_moment_twin_float_square(v);
            } else if (ones > 0u) {
                v = 1u;
                ones--;
            }
            float_moment_twin_put(pic, 0u, row, col, v);
        }
    }
}

/* Noise in the case's range on luma (or the target fill), mid-grey on chroma
 * (the moments are luma only); `salt` separates the frames and the two
 * pictures of a frame. */
static inline int float_moment_twin_fill(VmafPicture *pic, const FloatMomentTwinCase *c,
                                         unsigned salt)
{
    const int err = vmaf_picture_alloc(pic, VMAF_PIX_FMT_YUV420P, c->bpc, c->w, c->h);
    if (err) {
        return err;
    }
    if (c->target != 0u) {
        float_moment_twin_fill_sum(pic, c);
    }
    const unsigned span = c->hi - c->lo + 1u;
    for (unsigned row = 0; row < pic->h[0] && c->target == 0u; row++) {
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
    if (gpu_err == -ENOSYS) {
        /* A build without the device kernels (HIP with enable_hipcc=false). */
        (void)fprintf(stderr, "[skip: %s kernels not built] ", twin->backend);
        mu_skipped = 1;
        return 1;
    }
    const int cpu_err = gpu_err ? 0 : float_moment_twin_scores(twin, NULL, c, cpu);
    if (gpu_err || cpu_err || close_err) {
        (void)fprintf(stderr, "\n%s: run failed (%s %d, cpu %d, close %d)\n", c->name,
                      twin->backend, gpu_err, cpu_err, close_err);
        return gpu_err ? gpu_err : (cpu_err ? cpu_err : -EIO);
    }
    return 0;
}

/* Outputs of `gpu` that are not the CPU's, each one reported. */
static inline unsigned float_moment_twin_compare(const FloatMomentTwin *twin,
                                                 const FloatMomentTwinCase *c,
                                                 const FloatMomentTwinScores *cpu,
                                                 const FloatMomentTwinScores *gpu)
{
    unsigned mismatches = 0u;
    for (unsigned i = 0; i < FLOAT_MOMENT_TWIN_FRAMES * FLOAT_MOMENT_TWIN_OUTPUTS; i++) {
        const unsigned frame = i / FLOAT_MOMENT_TWIN_OUTPUTS;
        const unsigned m = i % FLOAT_MOMENT_TWIN_OUTPUTS;
        const double a = cpu->v[frame][m];
        const double b = gpu->v[frame][m];
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
    return float_moment_twin_compare(twin, c, &cpu, &gpu);
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
    const FloatMomentTwinCase c = {"noise",          FIXTURE_W, FIXTURE_H, bpc, 0u,
                                   (1u << bpc) - 1u, 0u,        0u,        0u,  false};
    mu_assert("the float_moment twin is not bit-identical to the CPU on noise",
              float_moment_twin_mismatches(twin, &c) == 0u);
    return NULL;
}

/* 1920x1080 is below 2^21 pixels: the sum stays below 2^53 whatever the
 * content, so samples near the peak are exact too. */
static inline mu_message_t float_moment_twin_bright_1080p_exact(const FloatMomentTwin *twin)
{
    const FloatMomentTwinCase c = {
        "16-bit bright 1920x1080", 1920u, 1080u, 16u, 56000u, 65535u, 10u, 0u, 0u, false};
    mu_assert(
        "the float_moment twin is not bit-identical to the CPU on a bright 16-bit 1080p frame",
        float_moment_twin_mismatches(twin, &c) == 0u);
    return NULL;
}

/* The 16-bit cases past 2^53 units and at the boundary (see the top of the
 * file), shared with test_float_moment_sum. */
static const FloatMomentTwinCase FLOAT_MOMENT_TWIN_PAST_CASES[] = {
    {"16-bit 2560x1440 past 2^53", 2560u, 1440u, 16u, 64000u, 65535u, 10u, 0u, 0u, true},
    {"16-bit 4096x2560 past 2^55", 4096u, 2560u, 16u, 65000u, 65535u, 10u, 0u, 0u, true},
    {"16-bit sum 2^53 - 1", 2048u, 1032u, 16u, 0u, 0u, 0u, ((uint64_t)1 << 53) - 1u, 0u, false},
    {"16-bit sum 2^53", 2048u, 1032u, 16u, 0u, 0u, 0u, (uint64_t)1 << 53, 0u, false},
    {"16-bit sum 2^53 and three ties", 2048u, 1032u, 16u, 0u, 0u, 0u, (uint64_t)1 << 53, 3u, true},
};
#define FLOAT_MOMENT_TWIN_PAST_CASE_COUNT                                                          \
    (sizeof(FLOAT_MOMENT_TWIN_PAST_CASES) / sizeof(FLOAT_MOMENT_TWIN_PAST_CASES[0]))

/* The exact integer sum of the float squares of the luma of frame `frame` of
 * the case's reference picture, in units of 1 / scaler^2, or 0 when the
 * picture cannot be made. */
static inline uint64_t float_moment_twin_exact_sum(const FloatMomentTwinCase *c, unsigned frame)
{
    VmafPicture pic;
    if (float_moment_twin_fill(&pic, c, (frame * 16u) + 1u)) {
        return 0u;
    }
    uint64_t sum = 0u;
    for (unsigned row = 0; row < pic.h[0]; row++) {
        const uint16_t *line =
            (const uint16_t *)((const uint8_t *)pic.data[0] + ((size_t)row * pic.stride[0]));
        for (unsigned col = 0; col < pic.w[0]; col++) {
            sum += float_moment_twin_float_square(line[col]);
        }
    }
    (void)vmaf_picture_unref(&pic);
    return sum;
}

/* The case reaches its range: a sum past 2^53 units (or its target), and,
 * where the case says the CPU rounds, a CPU second moment that is not the one
 * of the exact sum (a twin returning the exact sum would fail). */
static inline bool float_moment_twin_reaches(const FloatMomentTwinCase *c,
                                             const FloatMomentTwinScores *cpu)
{
    const double pixels = (double)c->w * (double)c->h;
    const uint64_t exact = float_moment_twin_exact_sum(c, 0u);
    const uint64_t expected = c->target != 0u ? c->target + c->ones : exact;
    const bool past = c->target != 0u || exact > ((uint64_t)1 << 53);
    const double exact_moment = ((double)exact / 65536.0) / pixels;
    const bool differs = exact_moment != cpu->v[0][FLOAT_MOMENT_TWIN_FIRST_SECOND];
    if (exact == expected && past && differs == c->rounds) {
        return true;
    }
    (void)fprintf(stderr, "\n%s: exact sum %llu (2^%.4f), CPU %.17g, exact moment %.17g\n", c->name,
                  (unsigned long long)exact, log2((double)exact),
                  cpu->v[0][FLOAT_MOMENT_TWIN_FIRST_SECOND], exact_moment);
    return false;
}

/* 16-bit frames past 2^53 units and at the boundary (see the top of the
 * file): every output equal to the CPU's, and every case in its range. */
static inline mu_message_t float_moment_twin_past_2_53_exact(const FloatMomentTwin *twin)
{
    static FloatMomentTwinScores cpu;
    static FloatMomentTwinScores gpu;
    unsigned mismatches = 0u;
    for (size_t k = 0; k < FLOAT_MOMENT_TWIN_PAST_CASE_COUNT; k++) {
        const FloatMomentTwinCase *c = &FLOAT_MOMENT_TWIN_PAST_CASES[k];
        const int run = float_moment_twin_run(twin, c, &cpu, &gpu);
        mu_assert("the runs past 2^53 failed", run >= 0);
        if (run > 0) {
            return NULL;
        }
        mu_assert("a case past 2^53 does not reach its range", float_moment_twin_reaches(c, &cpu));
        mismatches += float_moment_twin_compare(twin, c, &cpu, &gpu);
    }
    mu_assert("the float_moment twin is not bit-identical to the CPU past 2^53", mismatches == 0u);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */

#endif /* LIBVMAF_TEST_FLOAT_MOMENT_TWIN_PARITY_H_ */
