/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * Integer VIF CPU vs. SYCL: the twin rounds where the CPU rounds
 * (first added as a places=3 parity test of scale 0).
 *
 * The CPU path is integer_vif.c; the SYCL path is integer_vif_sycl.cpp. Both
 * accumulate in int64 and then leave integer arithmetic. integer_vif.c stores
 * each scale's numerator and denominator in a `float`, adds those rounded
 * values for the debug outputs and divides in single precision. The twin kept
 * all of it in `double`, which put every scale of every frame up to 3.5e-7
 * from the CPU. It now rounds at the same three points, and this test pins
 * what follows from that:
 *
 *   - the denominator sums, which the kernels accumulate exactly as the CPU
 *     does, are the CPU's bit for bit, per scale and over the frame;
 *   - every score and every numerator sum is a single-precision value, as on
 *     the CPU, and within 5e-6 (relative) of the CPU's. They are not all
 *     equal yet: the kernel forms the gain in fp32 where integer_vif.c uses
 *     fp64, which moves a numerator by an fp32 step or a few on some frames
 *     (T-SYCL-VIF-FP32-GAIN-2026-10-01);
 *   - at 8 and at 10 bits, with `debug=true` and with `vif_skip_scale0=true`,
 *     where scale 0 is published as 0.
 *
 * It also pins the default set of outputs: without `debug` the twin emits the
 * four scores only, as the CPU extractor does. Its `debug` option defaulted
 * to true before.
 *
 * The denominator, single-precision and default-set checks fail on the old
 * twin.
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
    const char *option; /* NULL, or an option set to "true" on both sides */
    unsigned n_keys;
    const char *keys[MAX_KEYS];
} VifCase;

/* Largest relative distance of a score or numerator sum from the CPU's while
 * the kernel's gain is fp32. Measured: 1.2e-6 on the 10-bit fixture. */
#define SINGLE_STEP_TOLERANCE 5.0e-6

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

/* A gradient with texture and a frame-dependent phase for the reference; the
 * distorted frame adds a small periodic error. The left third is flat, so the
 * statistic takes its low-variance branch there and its logarithm branch in
 * the rest. */
static int fill_picture(VmafPicture *pic, unsigned bpc, unsigned frame, bool distorted)
{
    const int err = vmaf_picture_alloc(pic, VMAF_PIX_FMT_YUV420P, bpc, FIXTURE_W, FIXTURE_H);
    if (err)
        return err;
    const unsigned gain = 1u << (bpc - 8u);
    for (unsigned row = 0; row < pic->h[0]; row++) {
        for (unsigned col = 0; col < pic->w[0]; col++) {
            unsigned value = 96u * gain;
            if (col >= pic->w[0] / 3u) {
                value = (row + col + frame * 7u + ((row * 5u) ^ (col * 3u)) % 23u) * gain +
                        (row * col) % gain;
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
        mu_assert("fill reference failed", !fill_picture(&ref, c->bpc, frame, false));
        mu_assert("fill distorted failed", !fill_picture(&dist, c->bpc, frame, true));
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
                  !vmaf_feature_dictionary_set(&opts, c->option, "true"));
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

/* The outputs the kernels accumulate exactly as the CPU does: equal to the
 * last bit once the host rounds as the CPU does. */
static bool is_denominator(const char *key)
{
    return strstr(key, "vif_den") != NULL;
}

/* The frame ratio of the debug outputs is a double quotient of two sums of
 * floats; every other output is a float widened to double. */
static bool is_single_precision(const char *key)
{
    return strcmp(key, "integer_vif") != 0 && strcmp(key, "integer_vif_num") != 0 &&
           strcmp(key, "integer_vif_den") != 0;
}

static char *require_cpu_rounding(const VifCase *c, const VifScores *cpu, const VifScores *gpu)
{
    unsigned denominators_differing = 0u;
    unsigned not_single = 0u;
    unsigned too_far = 0u;
    for (unsigned frame = 0; frame < NUM_FRAMES; frame++) {
        for (unsigned key = 0; key < c->n_keys; key++) {
            const double a = cpu->v[frame][key];
            const double b = gpu->v[frame][key];
            mu_assert("the CPU vif output is not finite", isfinite(a));
            const bool same = score_bits(a) == score_bits(b);
            const bool single = !is_single_precision(c->keys[key]) || (double)(float)b == b;
            const bool close = fabs(a - b) <= SINGLE_STEP_TOLERANCE * fabs(a);
            if (is_denominator(c->keys[key]) && !same)
                denominators_differing++;
            not_single += single ? 0u : 1u;
            too_far += close ? 0u : 1u;
            if ((is_denominator(c->keys[key]) && !same) || !single || !close) {
                (void)fprintf(stderr, "\n%s frame %u %s: cpu=%.17g sycl=%.17g delta=%.3e", c->name,
                              frame, c->keys[key], a, b, fabs(a - b));
            }
        }
    }
    if (denominators_differing + not_single + too_far != 0u)
        (void)fprintf(stderr, "\n");
    mu_assert("vif_sycl's denominator sums must be the CPU's bit for bit",
              denominators_differing == 0u);
    mu_assert("vif_sycl must publish single-precision values, as integer_vif.c does",
              not_single == 0u);
    mu_assert("vif_sycl is more than 5e-6 (relative) from the CPU", too_far == 0u);
    return NULL;
}

/* One case on the CPU and on the twin. `cpu_out`, when not NULL, receives the
 * CPU's scores. */
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
        msg = require_cpu_rounding(c, &cpu, &gpu);
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

static char *test_vif_scales_round_as_the_cpu(void)
{
    static const VifCase c = {.name = "default", .bpc = 8u, .n_keys = 4u, .keys = {SCALE_KEYS}};
    return compare(&c, NULL);
}

/* debug=true publishes the frame ratio and the sums it is formed from. The
 * CPU adds the per-scale sums after rounding each to fp32, so the frame's
 * denominator is equal too. */
static char *test_vif_debug_outputs_round_as_the_cpu(void)
{
    static const VifCase c = {
        .name = "debug", .bpc = 8u, .option = "debug", .n_keys = 15u, .keys = {DEBUG_KEYS}};
    return compare(&c, NULL);
}

static char *test_vif_10bit_rounds_as_the_cpu(void)
{
    static const VifCase c = {.name = "10-bit", .bpc = 10u, .n_keys = 4u, .keys = {SCALE_KEYS}};
    return compare(&c, NULL);
}

/* vif_skip_scale0 must reach the emission site, not just the aggregate: the
 * CPU never computes scale 0 in this mode and publishes 0.0 for its score.
 * The score is filed under the derived key: the alias of
 * "VMAF_integer_feature_vif_scale0_score" is "integer_vif_scale0" and the
 * option alias "ssclz" is appended. */
static char *test_vif_skip_scale0_score_is_zero(void)
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

char *run_tests(void)
{
    mu_run_test(test_vif_sycl_registered);
    mu_run_test(test_vif_scales_round_as_the_cpu);
    mu_run_test(test_vif_debug_outputs_round_as_the_cpu);
    mu_run_test(test_vif_10bit_rounds_as_the_cpu);
    mu_run_test(test_vif_skip_scale0_score_is_zero);
    mu_run_test(test_vif_default_outputs_are_the_cpu_set);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
