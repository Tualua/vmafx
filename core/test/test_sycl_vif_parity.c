/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * Integer VIF CPU vs. SYCL: the twin returns the CPU's outputs bit for bit
 * (ADR-1432; first added as a places=3 parity test of scale 0).
 *
 * The CPU path is integer_vif.c; the SYCL path is integer_vif_sycl.cpp. Two
 * things made the twin differ from the CPU, and both are gone:
 *
 *   - integer_vif.c stores each scale's numerator and denominator in a
 *     `float` and divides in single precision; the twin kept them in
 *     `double` (every score up to 3.5e-7 off);
 *   - integer_vif.c forms a pixel's gain in fp64 and truncates two results
 *     to integers; the kernel used fp32 (ADR-0220: no fp64 on the device),
 *     which put a share of those integers one off and a numerator sum one or
 *     a few fp32 steps away. The kernel now computes the two integers exactly
 *     (feature/sycl/sycl_integer_vif_math.h).
 *
 * So this test asserts equality on every output of every frame:
 *
 *   - the four per-scale scores, at 8 and at 10 bits;
 *   - with `debug=true`, the frame ratio and the ten numerator / denominator
 *     sums as well;
 *   - with `vif_enhn_gain_limit=1.0`, the value the NEG models set, where a
 *     gain at the limit is the common case;
 *   - with `vif_skip_scale0=true`, where scale 0 is published as 0.
 *   - at 3840x1600, 10 bits, on seeded noise over the texture, with `debug`:
 *     the frame size of the zero-copy workload, where scale 0 runs the tiled
 *     horizontal kernel of ADR-1769 K1 over many work-groups and a race
 *     between them would show (the class vif_fused had at 4K only).
 *
 * It also pins the default set of outputs: without `debug` the twin emits the
 * four scores only, as the CPU extractor does.
 *
 * Every equality case fails on the twin before ADR-1432.
 *
 * Skip behaviour: exits 77 when there is no SYCL device.
 */

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
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

#ifndef FIXTURE_W
#define FIXTURE_W 256u
#endif
#ifndef FIXTURE_H
#define FIXTURE_H 144u
#endif

#define NUM_FRAMES 3u
#define MAX_KEYS 15u

typedef struct VifCase {
    const char *name;
    unsigned bpc;
    unsigned w; /* 0: FIXTURE_W x FIXTURE_H */
    unsigned h;
    uint32_t noise;     /* 0: none; else the seed of a per-sample noise term */
    const char *option; /* NULL, or an option set on both sides */
    const char *value;  /* its value; NULL means "true" */
    unsigned n_keys;
    const char *keys[MAX_KEYS];
} VifCase;

typedef struct VifScores {
    double v[NUM_FRAMES][MAX_KEYS];
} VifScores;

#define SCALE_KEYS                                                                                 \
    "VMAF_integer_feature_vif_scale0_score", "VMAF_integer_feature_vif_scale1_score",              \
        "VMAF_integer_feature_vif_scale2_score", "VMAF_integer_feature_vif_scale3_score"

#define DEBUG_KEYS                                                                                 \
    SCALE_KEYS, "integer_vif", "integer_vif_num", "integer_vif_den", "integer_vif_num_scale0",     \
        "integer_vif_den_scale0", "integer_vif_num_scale1", "integer_vif_den_scale1",              \
        "integer_vif_num_scale2", "integer_vif_den_scale2", "integer_vif_num_scale3",              \
        "integer_vif_den_scale3"

static void put_sample(VmafPicture *pic, unsigned plane, unsigned row, unsigned col, unsigned value)
{
    const unsigned peak = (1u << pic->bpc) - 1u;
    uint8_t *line = (uint8_t *)pic->data[plane] + (size_t)row * (size_t)pic->stride[plane];
    if (pic->bpc <= 8u) {
        line[col] = (uint8_t)(value & peak);
    } else {
        ((uint16_t *)line)[col] = (uint16_t)(value & peak);
    }
}

/* A hash of the sample position: the seeded noise of the large case. */
static unsigned sample_noise(uint32_t seed, unsigned row, unsigned col, unsigned frame)
{
    uint32_t x = seed ^ (row * 0x9E3779B1u) ^ (col * 0x85EBCA77u) ^ (frame * 0xC2B2AE3Du);
    x ^= x >> 15;
    x *= 0x2C1B3C6Du;
    x ^= x >> 12;
    return x & 63u;
}

/* A gradient with texture and a frame-dependent phase for the reference; the
 * distorted frame adds a small periodic error. The left third is flat, so the
 * statistic takes its low-variance branch there and its logarithm branch in
 * the rest. A case with `noise` adds seeded noise to the textured part. */
static int fill_picture(VmafPicture *pic, const VifCase *c, unsigned frame, bool distorted)
{
    const unsigned bpc = c->bpc;
    const int err = vmaf_picture_alloc(pic, VMAF_PIX_FMT_YUV420P, bpc, c->w ? c->w : FIXTURE_W,
                                       c->h ? c->h : FIXTURE_H);
    if (err)
        return err;
    const unsigned gain = 1u << (bpc - 8u);
    for (unsigned row = 0; row < pic->h[0]; row++) {
        for (unsigned col = 0; col < pic->w[0]; col++) {
            unsigned value = 96u * gain;
            if (col >= pic->w[0] / 3u) {
                value = (row + col + frame * 7u + ((row * 5u) ^ (col * 3u)) % 23u) * gain +
                        (row * col) % gain;
                if (c->noise)
                    value += sample_noise(c->noise, row, col, frame) * gain;
                if (distorted)
                    value += ((row * 2u + col + frame * 3u) % 13u) * gain;
            }
            put_sample(pic, 0u, row, col, value);
        }
    }
    for (unsigned plane = 1; plane < 3; plane++) {
        for (unsigned row = 0; row < pic->h[plane]; row++) {
            for (unsigned col = 0; col < pic->w[plane]; col++)
                put_sample(pic, plane, row, col, (row * 3u + col * 5u + plane * 17u) * gain);
        }
    }
    return 0;
}

static char *feed(VmafContext *vmaf, const VifCase *c)
{
    for (unsigned frame = 0; frame < NUM_FRAMES; frame++) {
        VmafPicture ref;
        VmafPicture dist;
        mu_assert("fill reference failed", !fill_picture(&ref, c, frame, false));
        mu_assert("fill distorted failed", !fill_picture(&dist, c, frame, true));
        mu_assert("vmaf_read_pictures failed", !vmaf_read_pictures(vmaf, &ref, &dist, frame));
    }
    mu_assert("vmaf_read_pictures(EOS) failed", !vmaf_read_pictures(vmaf, NULL, NULL, 0));
    return NULL;
}

static char *read_scores(VmafContext *vmaf, const VifCase *c, VifScores *out)
{
    for (unsigned frame = 0; frame < NUM_FRAMES; frame++) {
        for (unsigned key = 0; key < c->n_keys; key++) {
            if (vmaf_feature_score_at_index(vmaf, c->keys[key], &out->v[frame][key], frame)) {
                (void)fprintf(stderr, "\n%s: no score for %s at frame %u\n", c->name, c->keys[key],
                              frame);
                return "vmaf_feature_score_at_index failed";
            }
        }
    }
    return NULL;
}

/* A context that has scored the case's frames on the CPU (`state` NULL) or on
 * the twin. The caller closes it. */
static char *score_context(VmafSyclState *state, const VifCase *c, VmafContext **vmaf)
{
    VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    mu_assert("vmaf_init failed", !vmaf_init(vmaf, cfg));
    if (state)
        mu_assert("vmaf_sycl_import_state failed", !vmaf_sycl_import_state(*vmaf, state));
    VmafFeatureDictionary *opts = NULL;
    if (c->option) {
        mu_assert("vmaf_feature_dictionary_set failed",
                  !vmaf_feature_dictionary_set(&opts, c->option, c->value ? c->value : "true"));
    }
    /* vmaf_use_feature() takes the dictionary over, on failure too. */
    mu_assert("vmaf_use_feature failed",
              !vmaf_use_feature(*vmaf, state ? "vif_sycl" : "vif", opts));
    return feed(*vmaf, c);
}

static char *score(VmafSyclState *state, const VifCase *c, VifScores *out)
{
    VmafContext *vmaf = NULL;
    char *msg = score_context(state, c, &vmaf);
    if (!msg)
        msg = read_scores(vmaf, c, out);
    if (vmaf != NULL && vmaf_close(vmaf) != 0 && !msg)
        msg = "vmaf_close failed";
    return msg;
}

/* A device state of its own per comparison: a SYCL state keeps the geometry of
 * its first frame. NULL without a device, after marking the test skipped. */
static VmafSyclState *open_device(void)
{
    VmafSyclState *state = NULL;
    VmafSyclConfiguration sycl_cfg = {.device_index = -1};
    if (vmaf_sycl_state_init(&state, sycl_cfg) != 0 || state == NULL) {
        (void)fprintf(stderr, "[skip: no SYCL device] ");
        mu_skipped = 1;
        return NULL;
    }
    return state;
}

static uint64_t score_bits(double value)
{
    uint64_t bits = 0u;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static char *require_identical(const VifCase *c, const VifScores *cpu, const VifScores *gpu)
{
    unsigned differing = 0u;
    for (unsigned frame = 0; frame < NUM_FRAMES; frame++) {
        for (unsigned key = 0; key < c->n_keys; key++) {
            const double a = cpu->v[frame][key];
            const double b = gpu->v[frame][key];
            mu_assert("the CPU vif output is not finite", isfinite(a));
            if (score_bits(a) == score_bits(b))
                continue;
            differing++;
            (void)fprintf(stderr, "\n%s frame %u %s: cpu=%.17g sycl=%.17g delta=%.3e", c->name,
                          frame, c->keys[key], a, b, fabs(a - b));
        }
    }
    if (differing != 0u)
        (void)fprintf(stderr, "\n");
    mu_assert("vif_sycl must return the CPU's vif outputs bit for bit (ADR-1432)", differing == 0u);
    return NULL;
}

/* One case on the CPU and on the twin, compared exactly. `cpu_out`, when not
 * NULL, receives the CPU's scores. */
static char *compare(const VifCase *c, VifScores *cpu_out)
{
    VmafSyclState *state = open_device();
    if (state == NULL)
        return NULL;
    static VifScores cpu;
    static VifScores gpu;
    char *msg = score(NULL, c, &cpu);
    if (!msg)
        msg = score(state, c, &gpu);
    if (!msg)
        msg = require_identical(c, &cpu, &gpu);
    if (cpu_out != NULL)
        *cpu_out = cpu;
    vmaf_sycl_state_free(&state);
    return msg;
}

static char *test_vif_sycl_registered(void)
{
    VmafFeatureExtractor *fex = vmaf_get_feature_extractor_by_name("vif_sycl");
    mu_assert("vif_sycl extractor must be registered", fex != NULL);
    mu_assert("vif_sycl name matches", !strcmp(fex->name, "vif_sycl"));
    return NULL;
}

static char *test_vif_scales_identical(void)
{
    static const VifCase c = {.name = "default", .bpc = 8u, .n_keys = 4u, .keys = {SCALE_KEYS}};
    return compare(&c, NULL);
}

/* debug=true publishes the frame ratio and the sums it is formed from. The
 * CPU adds the per-scale sums after rounding each to fp32. */
static char *test_vif_debug_outputs_identical(void)
{
    static const VifCase c = {
        .name = "debug", .bpc = 8u, .option = "debug", .n_keys = 15u, .keys = {DEBUG_KEYS}};
    return compare(&c, NULL);
}

/* A gain at the limit takes fl64(limit * limit * sigma1_sq), not the
 * quotient: with the limit at 1 that is the common case on this fixture. The
 * key carries the option's alias and value. */
static char *test_vif_gain_limit_identical(void)
{
    static const VifCase c = {
        .name = "egl=1",
        .bpc = 8u,
        .option = "vif_enhn_gain_limit",
        .value = "1.0",
        .n_keys = 4u,
        .keys = {"integer_vif_scale0_egl_1", "integer_vif_scale1_egl_1", "integer_vif_scale2_egl_1",
                 "integer_vif_scale3_egl_1"},
    };
    return compare(&c, NULL);
}

static char *test_vif_10bit_identical(void)
{
    static const VifCase c = {.name = "10-bit", .bpc = 10u, .n_keys = 4u, .keys = {SCALE_KEYS}};
    return compare(&c, NULL);
}

/* ADR-1769 K1: the zero-copy workload's frame size, 10 bits, with every debug
 * sum. Scale 0 runs the tiled horizontal kernel over 400 x 60 work-groups. */
static char *test_vif_3840x1600_10bit_identical(void)
{
    static const VifCase c = {.name = "3840x1600 10-bit",
                              .bpc = 10u,
                              .w = 3840u,
                              .h = 1600u,
                              .noise = 0x13C5u,
                              .option = "debug",
                              .n_keys = 15u,
                              .keys = {DEBUG_KEYS}};
    return compare(&c, NULL);
}

/* vif_skip_scale0 must reach the emission site, not just the aggregate: the
 * CPU never computes scale 0 in this mode and publishes 0.0 for its score.
 * The score is filed under the derived key: the alias of
 * "VMAF_integer_feature_vif_scale0_score" is "integer_vif_scale0" and the
 * option alias "ssclz" is appended. */
static char *test_vif_skip_scale0_identical(void)
{
    static const VifCase c = {
        .name = "vif_skip_scale0",
        .bpc = 8u,
        .option = "vif_skip_scale0",
        .n_keys = 4u,
        .keys = {"integer_vif_scale0_ssclz", "integer_vif_scale1_ssclz", "integer_vif_scale2_ssclz",
                 "integer_vif_scale3_ssclz"},
    };
    static VifScores cpu;
    mu_assert_msg(compare(&c, &cpu));
    if (!mu_skipped) {
        mu_assert("vif_skip_scale0: the scale 0 score must be exactly 0.0", cpu.v[1][0] == 0.0);
    }
    return NULL;
}

/* Without `debug` neither side publishes the debug outputs. */
static char *test_vif_default_outputs_are_the_cpu_set(void)
{
    static const VifCase c = {.name = "default set", .bpc = 8u, .n_keys = 0u};
    VmafSyclState *state = open_device();
    if (state == NULL)
        return NULL;
    VmafContext *cpu = NULL;
    VmafContext *gpu = NULL;
    char *msg = score_context(NULL, &c, &cpu);
    if (!msg)
        msg = score_context(state, &c, &gpu);
    double value = 0.0;
    const bool cpu_has =
        !msg && vmaf_feature_score_at_index(cpu, "integer_vif_num", &value, 0u) == 0;
    const bool gpu_has =
        !msg && vmaf_feature_score_at_index(gpu, "integer_vif_num", &value, 0u) == 0;
    if (cpu != NULL && vmaf_close(cpu) != 0 && !msg)
        msg = "vmaf_close failed";
    if (gpu != NULL && vmaf_close(gpu) != 0 && !msg)
        msg = "vmaf_close failed";
    vmaf_sycl_state_free(&state);
    mu_assert_msg(msg);
    mu_assert("the CPU vif extractor publishes integer_vif_num without debug", !cpu_has);
    mu_assert("vif_sycl publishes integer_vif_num without debug; the CPU does not", !gpu_has);
    return NULL;
}

static char *run_tests_identical(void)
{
    mu_run_test(test_vif_scales_identical);
    mu_run_test(test_vif_debug_outputs_identical);
    mu_run_test(test_vif_gain_limit_identical);
    mu_run_test(test_vif_10bit_identical);
    mu_run_test(test_vif_3840x1600_10bit_identical);
    mu_run_test(test_vif_skip_scale0_identical);
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_vif_sycl_registered);
    mu_run_test(run_tests_identical);
    mu_run_test(test_vif_default_outputs_are_the_cpu_set);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
