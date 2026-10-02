/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * float_moment CPU vs. HIP: the twin returns the CPU's four moments bit for
 * bit (ADR-1447; first added as a places=4 parity test, ADR-1212).
 *
 * moment.c adds the samples, and their squares, into one double per output.
 * It forms each square in float. Up to 12 bits a square has at most 24
 * significant bits, so the float is the integer square; at 16 bits it is the
 * square rounded to 24 bits. `float_moment_hip` added the exact integer
 * squares, which is the CPU's sum up to 12 bits and not at 16: on full-range
 * 16-bit noise the second moments were 2.8e-5 off. It adds the float squares
 * now.
 *
 * Natural 16-bit fixtures do not show the difference when they hold 8-bit
 * content shifted left (the squares are then multiples of 2^16 with few
 * significant bits), so the cases here are noise over a range of the bit
 * depth, independent for the reference and the distorted picture. Two frames
 * per case, every output compared with ==. On the old twin the 8-, 10- and
 * 12-bit cases pass and both 16-bit cases fail.
 *
 * The last case is the range in which the CPU's own sum is no longer exact.
 * Every term is a multiple of 2^-16 below 2^16, so the CPU's running double
 * sum is exact while it is below 2^53 units; it can reach that only on a
 * 16-bit frame of more than 2^21 pixels. From there the CPU rounds every add
 * of a term that is not a multiple of the sum's last place (the squares of
 * samples below 4096), and the twin, which holds the exact sum and rounds
 * once, may differ from it by at most the bound moment_sum_bound() derives.
 * The case is a 2560x1440 frame, nine tenths of it near the peak and one
 * tenth below 4096 (sum about 1.5 * 2^53). It asserts the bound, that the
 * frame really is past 2^53, and that the CPU's sum did round there (about
 * 2.7e-7 on this fixture), so the case cannot pass by not reaching the range.
 *
 * Skip behaviour: without a HIP device, or on a build without the device
 * kernels (-ENOSYS), a case reports the skip and the run exits 77.
 */

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
#include "libvmaf/libvmaf_hip.h"
#include "libvmaf/picture.h"

/* NOLINTBEGIN(modernize-use-nullptr): this is a
 * C23 translation unit, but the required MSVC C lane does not provide the C
 * nullptr spelling clang-tidy proposes. Keep the portable C API form under
 * ADR-1138. */

#ifndef FIXTURE_W
#define FIXTURE_W 256u
#endif
#ifndef FIXTURE_H
#define FIXTURE_H 144u
#endif

#define NUM_FRAMES 2u
#define NUM_MOMENTS 4u
#define FIRST_SECOND_MOMENT 2u

static const char *const MOMENT_FEATURES[NUM_MOMENTS] = {
    "float_moment_ref1st",
    "float_moment_dis1st",
    "float_moment_ref2nd",
    "float_moment_dis2nd",
};

#define DARK_PEAK 4095u

/* A frame of noise: luma samples uniform in [lo, hi], and `dark_percent` of
 * them uniform in [1, DARK_PEAK] instead. */
typedef struct MomentCase {
    const char *name;
    unsigned w;
    unsigned h;
    unsigned bpc;
    unsigned lo;
    unsigned hi;
    unsigned dark_percent;
} MomentCase;

typedef struct MomentScores {
    double v[NUM_FRAMES][NUM_MOMENTS];
} MomentScores;

/* lowbias32 hash of the position, the frame and the picture: stateless, so
 * both runs see the same pictures. */
static uint32_t sample_hash(unsigned row, unsigned col, unsigned salt)
{
    uint32_t x = ((uint32_t)row << 16) ^ (uint32_t)col ^ (salt * 0x9E3779B9u);
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static void put_sample(VmafPicture *pic, unsigned plane, unsigned row, unsigned col, unsigned v)
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
static int fill_picture(VmafPicture *pic, const MomentCase *c, unsigned salt)
{
    const int err = vmaf_picture_alloc(pic, VMAF_PIX_FMT_YUV420P, c->bpc, c->w, c->h);
    if (err) {
        return err;
    }
    const unsigned span = c->hi - c->lo + 1u;
    for (unsigned row = 0; row < pic->h[0]; row++) {
        for (unsigned col = 0; col < pic->w[0]; col++) {
            const uint32_t hash = sample_hash(row, col, salt);
            const bool dark = (sample_hash(col, row, salt + 3u) % 100u) < c->dark_percent;
            put_sample(pic, 0u, row, col, dark ? 1u + (hash % DARK_PEAK) : c->lo + (hash % span));
        }
    }
    for (unsigned p = 1; p < 3u; p++) {
        for (unsigned row = 0; row < pic->h[p]; row++) {
            for (unsigned col = 0; col < pic->w[p]; col++) {
                put_sample(pic, p, row, col, 1u << (c->bpc - 1u));
            }
        }
    }
    return 0;
}

/* Frame `frame` of the case through `vmaf`, which takes both pictures. */
static int feed_frame(VmafContext *vmaf, const MomentCase *c, unsigned frame)
{
    VmafPicture ref;
    VmafPicture dist;
    int err = fill_picture(&ref, c, (frame * 16u) + 1u);
    if (err) {
        return err;
    }
    err = fill_picture(&dist, c, (frame * 16u) + 8u);
    if (err) {
        (void)vmaf_picture_unref(&ref);
        return err;
    }
    return vmaf_read_pictures(vmaf, &ref, &dist, frame);
}

/* NUM_FRAMES frames of the case through the CPU `float_moment` (`hip_state`
 * NULL) or the twin, and every output of every frame read into `out`. Returns
 * the first error; -ENOSYS is the scaffold build. */
static int moment_scores(VmafHipState *hip_state, const MomentCase *c, MomentScores *out)
{
    const VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    VmafContext *vmaf = NULL;
    int err = vmaf_init(&vmaf, cfg);
    if (!err && hip_state) {
        err = vmaf_hip_import_state(vmaf, hip_state);
    }
    if (!err) {
        err = vmaf_use_feature(vmaf, hip_state ? "float_moment_hip" : "float_moment", NULL);
    }
    for (unsigned frame = 0; frame < NUM_FRAMES && !err; frame++) {
        err = feed_frame(vmaf, c, frame);
    }
    if (!err) {
        err = vmaf_read_pictures(vmaf, NULL, NULL, 0);
    }
    for (unsigned i = 0; i < NUM_FRAMES * NUM_MOMENTS && !err; i++) {
        const unsigned frame = i / NUM_MOMENTS;
        const unsigned m = i % NUM_MOMENTS;
        err = vmaf_feature_score_at_index(vmaf, MOMENT_FEATURES[m], &out->v[frame][m], frame);
    }
    const int closed = vmaf ? vmaf_close(vmaf) : 0;
    return err ? err : closed;
}

/* The CPU's and the twin's outputs for one case. Returns 0, a negative errno
 * when a run failed, or 1 when the HIP leg was skipped (no device, or a build
 * without the device kernels). */
static int run_case(const MomentCase *c, MomentScores *cpu, MomentScores *gpu)
{
    VmafHipState *hip_state = NULL;
    const VmafHipConfiguration hip_cfg = {.device_index = -1};
    if (vmaf_hip_state_init(&hip_state, hip_cfg) != 0 || hip_state == NULL) {
        (void)fprintf(stderr, "[skip: no HIP device] ");
        mu_skipped = 1;
        return 1;
    }
    const int gpu_err = moment_scores(hip_state, c, gpu);
    vmaf_hip_state_free(&hip_state);
    if (gpu_err == -ENOSYS) {
        (void)fprintf(stderr, "[skip: HIP kernels not built (enable_hipcc=false)] ");
        mu_skipped = 1;
        return 1;
    }
    const int cpu_err = gpu_err ? 0 : moment_scores(NULL, c, cpu);
    if (gpu_err || cpu_err) {
        (void)fprintf(stderr, "\n%s: run failed (hip %d, cpu %d)\n", c->name, gpu_err, cpu_err);
        return gpu_err ? gpu_err : cpu_err;
    }
    return 0;
}

/* Outputs of the case that are not the CPU's, each one reported; UINT32_MAX
 * when a run failed. A skipped HIP leg counts as 0. */
static unsigned exact_mismatches(const MomentCase *c)
{
    static MomentScores cpu;
    static MomentScores gpu;
    const int run = run_case(c, &cpu, &gpu);
    if (run != 0) {
        return run > 0 ? 0u : UINT32_MAX;
    }
    unsigned mismatches = 0u;
    for (unsigned i = 0; i < NUM_FRAMES * NUM_MOMENTS; i++) {
        const double a = cpu.v[i / NUM_MOMENTS][i % NUM_MOMENTS];
        const double b = gpu.v[i / NUM_MOMENTS][i % NUM_MOMENTS];
        if (isfinite(a) && a == b) {
            continue;
        }
        mismatches++;
        (void)fprintf(stderr, "\n%s frame %u %s: cpu=%.17g hip=%.17g delta=%.3e", c->name,
                      i / NUM_MOMENTS, MOMENT_FEATURES[i % NUM_MOMENTS], a, b, fabs(a - b));
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
static double moment_sum_bound(double pixels, double moment)
{
    const double units = moment * pixels * 65536.0;
    int binade = 53;
    while (ldexp(1.0, binade + 1) <= units * (1.0 + 0x1p-40)) {
        binade++;
    }
    const double rounding_adds = pixels - 0x1p21 + 1.0;
    return rounding_adds / pixels * ldexp(1.0, binade - 69) + 0x1p-37;
}

static char *test_float_moment_hip_registered(void)
{
    VmafFeatureExtractor *fex = vmaf_get_feature_extractor_by_name("float_moment_hip");
    mu_assert("float_moment_hip extractor must be registered", fex != NULL);
    mu_assert("float_moment_hip name matches", !strcmp(fex->name, "float_moment_hip"));
    return NULL;
}

static char *test_float_moment_8bit_exact(void)
{
    static const MomentCase c = {"8-bit noise", FIXTURE_W, FIXTURE_H, 8u, 0u, 255u, 0u};
    mu_assert("float_moment_hip is not bit-identical to the CPU at 8 bits",
              exact_mismatches(&c) == 0u);
    return NULL;
}

static char *test_float_moment_10bit_exact(void)
{
    static const MomentCase c = {"10-bit noise", FIXTURE_W, FIXTURE_H, 10u, 0u, 1023u, 0u};
    mu_assert("float_moment_hip is not bit-identical to the CPU at 10 bits",
              exact_mismatches(&c) == 0u);
    return NULL;
}

static char *test_float_moment_12bit_exact(void)
{
    static const MomentCase c = {"12-bit noise", FIXTURE_W, FIXTURE_H, 12u, 0u, 4095u, 0u};
    mu_assert("float_moment_hip is not bit-identical to the CPU at 12 bits",
              exact_mismatches(&c) == 0u);
    return NULL;
}

static char *test_float_moment_16bit_exact(void)
{
    static const MomentCase c = {"16-bit noise", FIXTURE_W, FIXTURE_H, 16u, 0u, 65535u, 0u};
    mu_assert("float_moment_hip is not bit-identical to the CPU at 16 bits",
              exact_mismatches(&c) == 0u);
    return NULL;
}

/* 1920x1080 is below 2^21 pixels: the sum stays below 2^53 whatever the
 * content, so samples near the peak are exact too. */
static char *test_float_moment_16bit_bright_exact(void)
{
    static const MomentCase c = {"16-bit bright 1920x1080", 1920u, 1080u, 16u, 56000u, 65535u, 10u};
    mu_assert("float_moment_hip is not bit-identical to the CPU on a bright 16-bit 1080p frame",
              exact_mismatches(&c) == 0u);
    return NULL;
}

/* One second moment past 2^53 against the CPU's: inside the derived bound, and
 * the frame past 2^53 units. Returns the distance, or a negative value when
 * either does not hold (reported). */
static double past_2_53_distance(const MomentCase *c, const char *feature, double cpu, double gpu)
{
    const double pixels = (double)c->w * (double)c->h;
    const double bound = moment_sum_bound(pixels, cpu);
    const double delta = fabs(cpu - gpu);
    if (cpu * pixels >= 0x1p37 && delta <= bound) {
        return delta;
    }
    (void)fprintf(stderr, "\n%s %s: cpu=%.17g hip=%.17g delta=%.3e bound=%.3e units=2^%.3f\n",
                  c->name, feature, cpu, gpu, delta, bound, log2(cpu * pixels * 65536.0));
    return -1.0;
}

/* Past 2^53: the first moments stay exact, the second moments are within the
 * derived bound of the CPU's sequentially rounded sum, and that sum did
 * round. */
static char *test_float_moment_16bit_past_2_53_within_bound(void)
{
    static const MomentCase c = {
        "16-bit 2560x1440 past 2^53", 2560u, 1440u, 16u, 64000u, 65535u, 10u};
    static MomentScores cpu;
    static MomentScores gpu;
    const int run = run_case(&c, &cpu, &gpu);
    mu_assert("the runs past 2^53 failed", run >= 0);
    if (run > 0) {
        return NULL;
    }
    double largest = 0.0;
    for (unsigned i = 0; i < NUM_FRAMES * NUM_MOMENTS; i++) {
        const unsigned m = i % NUM_MOMENTS;
        const double a = cpu.v[i / NUM_MOMENTS][m];
        const double b = gpu.v[i / NUM_MOMENTS][m];
        if (m < FIRST_SECOND_MOMENT) {
            mu_assert("a first moment past 2^53 is not the CPU's", isfinite(a) && a == b);
            continue;
        }
        const double delta = past_2_53_distance(&c, MOMENT_FEATURES[m], a, b);
        mu_assert("a second moment past 2^53 is outside the derived bound", delta >= 0.0);
        largest = delta > largest ? delta : largest;
    }
    (void)fprintf(stderr, "[past 2^53: largest distance %.3e] ", largest);
    mu_assert("the CPU's sum did not round past 2^53: the case no longer reaches that range",
              largest > 0.0);
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_float_moment_hip_registered);
    mu_run_test(test_float_moment_8bit_exact);
    mu_run_test(test_float_moment_10bit_exact);
    mu_run_test(test_float_moment_12bit_exact);
    mu_run_test(test_float_moment_16bit_exact);
    mu_run_test(test_float_moment_16bit_bright_exact);
    mu_run_test(test_float_moment_16bit_past_2_53_within_bound);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
