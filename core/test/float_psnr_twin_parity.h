/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * float_psnr CPU vs. a GPU twin: fixtures, the comparison and the cases, for
 * a backend's parity test to instantiate (ADR-1450 for SYCL; the design of
 * ADR-1440).
 *
 * float_psnr.c forms each squared difference in float and adds the terms in
 * double, row by row. Every term is a float and a multiple of 1 / scaler^2
 * (scaler = 2^(bpc - 8)), so that sum is exact while it is below 2^53 of
 * those units, and a twin returns the same score exactly when its own sum is
 * exact. A twin that adds a work-group's terms in fp32 is exact only while
 * the group's sum fits 24 bits in that unit: always at 8 bits (256 squares
 * below 2^16), and at 10, 12 and 16 bits only while the differences are
 * small.
 *
 * Natural content does not reach that, and the repository's high-bit-depth
 * fixtures are an 8-bit clip shifted left. The fixtures here are noise,
 * independent for the reference and the distorted frame, over a range of the
 * bit depth, so every group's sum of squares is far above 2^24 units. Two
 * frames per case, compared with ==.
 *
 * The last case is the range in which the CPU's own sum is no longer exact:
 * a 16-bit frame whose mean squared error times the pixel count reaches 2^37
 * on the 8-bit scale (a PSNR below 6 dB at 3840x2160). There the CPU rounds
 * each further add of a row's sum, and a twin that holds the exact sum and
 * rounds once is within float_psnr_twin_bound() of it.
 *
 * A test includes this header once, after defining FIXTURE_W / FIXTURE_H if
 * it wants another size for the noise cases, and provides a FloatPsnrTwin.
 * A missing device skips the case and the run exits 77.
 */

#ifndef LIBVMAF_TEST_FLOAT_PSNR_TWIN_PARITY_H_
#define LIBVMAF_TEST_FLOAT_PSNR_TWIN_PARITY_H_

#include <errno.h>
#include <float.h>
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

enum { FLOAT_PSNR_TWIN_FRAMES = 2 };

/* The backend under test. */
typedef struct FloatPsnrTwin {
    const char *extractor; /* "float_psnr_sycl", ... */
    const char *backend;   /* for messages */
    /* Opens a device state. Non-zero: no device, the case is skipped. */
    int (*open)(void **state);
    int (*import)(VmafContext *vmaf, void *state);
    int (*close)(void *state);
} FloatPsnrTwin;

/* Luma of the reference uniform in [ref_lo, ref_hi] and of the distorted
 * frame in [dis_lo, dis_hi], independent noise; `same` makes the distorted
 * frame the reference. */
typedef struct FloatPsnrTwinCase {
    const char *name;
    unsigned w;
    unsigned h;
    unsigned bpc;
    unsigned ref_lo;
    unsigned ref_hi;
    unsigned dis_lo;
    unsigned dis_hi;
    bool same;
    const char *option; /* a boolean option set to true, or NULL */
} FloatPsnrTwinCase;

/* lowbias32 hash of the position, the frame and the picture: stateless, so
 * both runs see the same pictures. */
static inline uint32_t float_psnr_twin_hash(unsigned row, unsigned col, unsigned salt)
{
    uint32_t x = ((uint32_t)row << 16) ^ (uint32_t)col ^ (salt * 0x9E3779B9u);
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static inline void float_psnr_twin_put(VmafPicture *pic, unsigned plane, unsigned row, unsigned col,
                                       unsigned v)
{
    uint8_t *line = (uint8_t *)pic->data[plane] + ((size_t)row * (size_t)pic->stride[plane]);
    if (pic->bpc <= 8u) {
        line[col] = (uint8_t)v;
    } else {
        ((uint16_t *)line)[col] = (uint16_t)v;
    }
}

/* Noise in [lo, hi] on luma, mid-grey on chroma (float_psnr is luma only);
 * `salt` separates the frames and the two pictures of a frame. */
static inline int float_psnr_twin_fill(VmafPicture *pic, const FloatPsnrTwinCase *c, unsigned lo,
                                       unsigned hi, unsigned salt)
{
    const int err = vmaf_picture_alloc(pic, VMAF_PIX_FMT_YUV420P, c->bpc, c->w, c->h);
    if (err) {
        return err;
    }
    const unsigned span = hi - lo + 1u;
    for (unsigned row = 0; row < pic->h[0]; row++) {
        for (unsigned col = 0; col < pic->w[0]; col++) {
            float_psnr_twin_put(pic, 0u, row, col,
                                lo + (float_psnr_twin_hash(row, col, salt) % span));
        }
    }
    for (unsigned p = 1; p < 3u; p++) {
        for (unsigned row = 0; row < pic->h[p]; row++) {
            for (unsigned col = 0; col < pic->w[p]; col++) {
                float_psnr_twin_put(pic, p, row, col, 1u << (c->bpc - 1u));
            }
        }
    }
    return 0;
}

/* Frame `frame` of the case through `vmaf`, which takes both pictures. */
static inline int float_psnr_twin_feed(VmafContext *vmaf, const FloatPsnrTwinCase *c,
                                       unsigned frame)
{
    VmafPicture ref;
    VmafPicture dist;
    const unsigned ref_salt = (frame * 16u) + 1u;
    int err = float_psnr_twin_fill(&ref, c, c->ref_lo, c->ref_hi, ref_salt);
    if (err) {
        return err;
    }
    err = c->same ? float_psnr_twin_fill(&dist, c, c->ref_lo, c->ref_hi, ref_salt) :
                    float_psnr_twin_fill(&dist, c, c->dis_lo, c->dis_hi, (frame * 16u) + 8u);
    if (err) {
        (void)vmaf_picture_unref(&ref);
        return err;
    }
    return vmaf_read_pictures(vmaf, &ref, &dist, frame);
}

/* The case's frames through the CPU `float_psnr` (`state` NULL) or the twin,
 * and the score of every frame read into `out`. Returns the first error. */
static inline int float_psnr_twin_scores(const FloatPsnrTwin *twin, void *state,
                                         const FloatPsnrTwinCase *c, double *out)
{
    const VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    VmafContext *vmaf = NULL;
    VmafFeatureDictionary *options = NULL;
    int err = vmaf_init(&vmaf, cfg);
    if (!err && state) {
        err = twin->import(vmaf, state);
    }
    if (!err && c->option) {
        err = vmaf_feature_dictionary_set(&options, c->option, "true");
    }
    if (!err) {
        err = vmaf_use_feature(vmaf, state ? twin->extractor : "float_psnr", options);
    }
    for (unsigned frame = 0; frame < FLOAT_PSNR_TWIN_FRAMES && !err; frame++) {
        err = float_psnr_twin_feed(vmaf, c, frame);
    }
    if (!err) {
        err = vmaf_read_pictures(vmaf, NULL, NULL, 0);
    }
    for (unsigned frame = 0; frame < FLOAT_PSNR_TWIN_FRAMES && !err; frame++) {
        err = vmaf_feature_score_at_index(vmaf, "float_psnr", &out[frame], frame);
    }
    const int closed = vmaf ? vmaf_close(vmaf) : 0;
    return err ? err : closed;
}

/* The CPU's and the twin's scores for one case. Returns 0, a negative errno
 * when a run failed, or 1 when the twin's leg was skipped (no device). */
static inline int float_psnr_twin_run(const FloatPsnrTwin *twin, const FloatPsnrTwinCase *c,
                                      double *cpu, double *gpu)
{
    void *state = NULL;
    if (twin->open(&state) != 0 || state == NULL) {
        (void)fprintf(stderr, "[skip: no %s device] ", twin->backend);
        mu_skipped = 1;
        return 1;
    }
    const int gpu_err = float_psnr_twin_scores(twin, state, c, gpu);
    const int close_err = twin->close(state);
    const int cpu_err = gpu_err ? 0 : float_psnr_twin_scores(twin, NULL, c, cpu);
    if (gpu_err || cpu_err || close_err) {
        (void)fprintf(stderr, "\n%s: run failed (%s %d, cpu %d, close %d)\n", c->name,
                      twin->backend, gpu_err, cpu_err, close_err);
        return gpu_err ? gpu_err : (cpu_err ? cpu_err : -EIO);
    }
    return 0;
}

/* Frames of the case whose twin score is not the CPU's, each one reported;
 * UINT32_MAX when a run failed. A skipped leg counts as 0. `first` returns
 * the CPU's score of frame 0. */
static inline unsigned float_psnr_twin_mismatches(const FloatPsnrTwin *twin,
                                                  const FloatPsnrTwinCase *c, double *first)
{
    double cpu[FLOAT_PSNR_TWIN_FRAMES] = {0.0};
    double gpu[FLOAT_PSNR_TWIN_FRAMES] = {0.0};
    const int run = float_psnr_twin_run(twin, c, cpu, gpu);
    if (run != 0) {
        return run > 0 ? 0u : UINT32_MAX;
    }
    unsigned mismatches = 0u;
    for (unsigned frame = 0; frame < FLOAT_PSNR_TWIN_FRAMES; frame++) {
        if (isfinite(cpu[frame]) && cpu[frame] == gpu[frame]) {
            continue;
        }
        mismatches++;
        (void)fprintf(stderr, "\n%s %ux%u %u-bit frame %u: cpu=%.17g %s=%.17g delta=%.3e\n",
                      c->name, c->w, c->h, c->bpc, frame, cpu[frame], twin->backend, gpu[frame],
                      fabs(cpu[frame] - gpu[frame]));
    }
    if (first) {
        *first = cpu[0];
    }
    return mismatches;
}

static inline mu_message_t float_psnr_twin_registered(const FloatPsnrTwin *twin)
{
    VmafFeatureExtractor *fex = vmaf_get_feature_extractor_by_name(twin->extractor);
    mu_assert("the float_psnr twin must be registered", fex != NULL);
    mu_assert("the float_psnr twin's name matches", !strcmp(fex->name, twin->extractor));
    return NULL;
}

/* Full-range noise at `bpc` bits, with `option` set when it is not NULL. */
static inline mu_message_t float_psnr_twin_noise_exact(const FloatPsnrTwin *twin, unsigned bpc,
                                                       const char *option)
{
    const unsigned peak = (1u << bpc) - 1u;
    const FloatPsnrTwinCase c = {"noise", FIXTURE_W, FIXTURE_H, bpc,   0u,
                                 peak,    0u,        peak,      false, option};
    mu_assert("the float_psnr twin is not bit-identical to the CPU on noise",
              float_psnr_twin_mismatches(twin, &c, NULL) == 0u);
    return NULL;
}

/* A bright 16-bit 1920x1080 pair: large samples, moderate differences. */
static inline mu_message_t float_psnr_twin_bright_1080p_exact(const FloatPsnrTwin *twin)
{
    const FloatPsnrTwinCase c = {"16-bit bright", 1920u,  1080u,  16u,   56000u,
                                 64000u,          56000u, 64000u, false, NULL};
    mu_assert("the float_psnr twin is not bit-identical to the CPU on a bright 16-bit frame",
              float_psnr_twin_mismatches(twin, &c, NULL) == 0u);
    return NULL;
}

/* Identical frames: zero noise, the score is the psnr_max sentinel. */
static inline mu_message_t float_psnr_twin_identical_exact(const FloatPsnrTwin *twin, unsigned bpc,
                                                           double psnr_max)
{
    const unsigned peak = (1u << bpc) - 1u;
    const FloatPsnrTwinCase c = {"identical", FIXTURE_W, FIXTURE_H, bpc,  0u,
                                 peak,        0u,        peak,      true, NULL};
    double first = 0.0;
    mu_assert("the float_psnr twin differs from the CPU on identical frames",
              float_psnr_twin_mismatches(twin, &c, &first) == 0u);
    mu_assert("identical frames must score psnr_max", mu_skipped || first == psnr_max);
    return NULL;
}

/* Largest distance between the CPU's score and the twin's on a frame of
 * `rows` rows whose sum of squares has passed 2^53 units.
 *
 * A row's sum is exact (at most 2^13 terms below 2^32 units). The CPU adds
 * the rows into one double; past 2^53 each add rounds by at most half a unit
 * in the last place of the sum, a relative 2^-53. The twin rounds the exact
 * sum once, by at most the same. So the two noises differ by a relative
 * (rows + 1) * 2^-53 at most, and a score of 10 * log10(peak^2 / noise) by
 * 10 / ln(10) times that, plus the roundings of the division and of log10 on
 * both sides, which four units in the last place of the score cover. */
static inline double float_psnr_twin_bound(unsigned rows, double score)
{
    const double relative = ((double)rows + 1.0) * 0x1p-53;
    return (10.0 / log(10.0)) * relative + 4.0 * DBL_EPSILON * fabs(score);
}

/* A 16-bit 2560x1440 frame, the reference near the peak and the distorted
 * frame near zero: the sum of squares is past 2^53 units. The twin is within
 * the bound, and the frame really is in that range. */
static inline mu_message_t float_psnr_twin_past_2_53_within_bound(const FloatPsnrTwin *twin)
{
    const FloatPsnrTwinCase c = {
        "16-bit past 2^53", 2560u, 1440u, 16u, 60000u, 65535u, 0u, 5000u, false, NULL};
    double cpu[FLOAT_PSNR_TWIN_FRAMES] = {0.0};
    double gpu[FLOAT_PSNR_TWIN_FRAMES] = {0.0};
    const int run = float_psnr_twin_run(twin, &c, cpu, gpu);
    mu_assert("the runs past 2^53 failed", run >= 0);
    if (run > 0) {
        return NULL;
    }
    const double peak = 255.99609375; /* float_psnr.c at 16 bits */
    double largest = 0.0;
    for (unsigned frame = 0; frame < FLOAT_PSNR_TWIN_FRAMES; frame++) {
        const double noise = peak * peak / pow(10.0, cpu[frame] / 10.0);
        const double units = noise * (double)c.w * (double)c.h * 65536.0;
        const double bound = float_psnr_twin_bound(c.h, cpu[frame]);
        const double delta = fabs(cpu[frame] - gpu[frame]);
        if (!(units >= 0x1p53) || !(delta <= bound)) {
            (void)fprintf(
                stderr, "\n%s frame %u: cpu=%.17g %s=%.17g delta=%.3e bound=%.3e units=2^%.3f\n",
                c.name, frame, cpu[frame], twin->backend, gpu[frame], delta, bound, log2(units));
        }
        mu_assert("the frame is not past 2^53 units: the case no longer reaches that range",
                  units >= 0x1p53);
        mu_assert("the float_psnr twin is outside the derived bound past 2^53", delta <= bound);
        largest = delta > largest ? delta : largest;
    }
    (void)fprintf(stderr, "[past 2^53: largest distance %.3e] ", largest);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */

#endif /* LIBVMAF_TEST_FLOAT_PSNR_TWIN_PARITY_H_ */
