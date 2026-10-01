/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * vif CPU vs. HIP: every output has the CPU's bits (ADR-1435).
 *
 * The fixed-point VIF statistic is integer arithmetic up to its last step:
 * int64 accumulators per scale, which integer_vif.c turns into two floats and
 * a single-precision ratio. `vif_hip` accumulates the same integers, so it
 * can return the CPU's values, and it does once every per-pixel logarithm is
 * the CPU's: the kernels read the log2 table integer_vif.c builds with the
 * host math library (vif_log2_table_generate()) instead of evaluating
 * log2f() on the device. The device's log2f() is one ulp from glibc's on
 * about half of the table's arguments, which moved 77 of its 32768 entries by
 * one and with them the numerator or denominator of nearly every frame.
 *
 * This test asserts equality, not a tolerance, on every output of every case:
 * the four scale scores and, with `debug=true`, the frame ratio and the sums
 * it is formed from. The registration with a 960x540 fixture
 * (`test_hip_vif_parity_large`) runs the same cases on more pixels. On the
 * twin that computed its logarithms on the device the first case fails at
 * both sizes, with scores up to 3.6e-7 from the CPU's.
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

#define MAX_KEYS 15u
/* Frames per run: the second one shows that a frame starts from cleared
 * accumulators. */
#define NUM_FRAMES 2u

/* One comparison: a bit depth, at most one option, and the keys to compare. */
typedef struct VifCase {
    const char *name;
    unsigned bpc;
    const char *option; /* NULL, or an option name */
    const char *value;  /* the option's value */
    size_t n_keys;
    const char *keys[MAX_KEYS];
} VifCase;

/* The four scores under their registered names, and under the names an
 * option set derives from the aliases ("integer_vif_scale0" + suffix). */
#define SCALE_KEYS                                                                                 \
    "VMAF_integer_feature_vif_scale0_score", "VMAF_integer_feature_vif_scale1_score",              \
        "VMAF_integer_feature_vif_scale2_score", "VMAF_integer_feature_vif_scale3_score"

#define OPTION_SCALE_KEYS(suffix)                                                                  \
    "integer_vif_scale0" suffix, "integer_vif_scale1" suffix, "integer_vif_scale2" suffix,         \
        "integer_vif_scale3" suffix

#define DEBUG_KEYS                                                                                 \
    SCALE_KEYS, "integer_vif", "integer_vif_num", "integer_vif_den", "integer_vif_num_scale0",     \
        "integer_vif_den_scale0", "integer_vif_num_scale1", "integer_vif_den_scale1",              \
        "integer_vif_num_scale2", "integer_vif_den_scale2", "integer_vif_num_scale3",              \
        "integer_vif_den_scale3"

static void put_sample(VmafPicture *pic, unsigned plane, unsigned row, unsigned col, unsigned value)
{
    const unsigned peak = (1u << pic->bpc) - 1u;
    uint8_t *line = (uint8_t *)pic->data[plane] + ((size_t)row * (size_t)pic->stride[plane]);
    if (pic->bpc <= 8u) {
        line[col] = (uint8_t)(value & peak);
    } else {
        ((uint16_t *)line)[col] = (uint16_t)(value & peak);
    }
}

/* Luma of the fixture: a wrapping ramp with texture and a frame-dependent
 * phase for the reference; the distorted frame adds a periodic error. The
 * left third is flat, so the statistic takes its low-variance branch there
 * and its logarithm branch, over a wide range of variances, in the rest. */
static unsigned luma(unsigned row, unsigned col, unsigned frame, bool distorted, unsigned gain)
{
    if (col < FIXTURE_W / 3u) {
        return 96u * gain;
    }
    unsigned value = ((row + col + (frame * 7u) + (((row * 5u) ^ (col * 3u)) % 23u)) * gain) +
                     ((row * col) % gain);
    if (distorted) {
        value += (((row * 2u) + col + (frame * 3u)) % 13u) * gain;
    }
    return value;
}

static int fill_picture(VmafPicture *pic, unsigned bpc, unsigned frame, bool distorted)
{
    const int err = vmaf_picture_alloc(pic, VMAF_PIX_FMT_YUV420P, bpc, FIXTURE_W, FIXTURE_H);
    if (err) {
        return err;
    }
    const unsigned gain = 1u << (bpc - 8u);
    for (unsigned row = 0; row < pic->h[0]; row++) {
        for (unsigned col = 0; col < pic->w[0]; col++) {
            put_sample(pic, 0u, row, col, luma(row, col, frame, distorted, gain));
        }
    }
    for (unsigned plane = 1; plane < 3; plane++) {
        for (unsigned row = 0; row < pic->h[plane]; row++) {
            for (unsigned col = 0; col < pic->w[plane]; col++) {
                put_sample(pic, plane, row, col, 128u * gain);
            }
        }
    }
    return 0;
}

/* Frame `frame` of the fixture through `vmaf`, which takes both pictures. */
static int feed_frame(VmafContext *vmaf, unsigned bpc, unsigned frame)
{
    VmafPicture ref;
    VmafPicture dist;
    int err = fill_picture(&ref, bpc, frame, false);
    if (err) {
        return err;
    }
    err = fill_picture(&dist, bpc, frame, true);
    if (err) {
        (void)vmaf_picture_unref(&ref);
        return err;
    }
    return vmaf_read_pictures(vmaf, &ref, &dist, frame);
}

/* A context with the CPU `vif` extractor, or with `vif_hip` on `hip_state`,
 * carrying the case's option. */
static int vif_context(VmafContext **vmaf, VmafHipState *hip_state, const VifCase *c)
{
    const VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    int err = vmaf_init(vmaf, cfg);
    if (!err && hip_state) {
        err = vmaf_hip_import_state(*vmaf, hip_state);
    }
    VmafFeatureDictionary *opts = NULL;
    if (!err && c->option) {
        err = vmaf_feature_dictionary_set(&opts, c->option, c->value);
    }
    if (!err) {
        /* vmaf_use_feature() takes the dictionary over, on failure too. */
        err = vmaf_use_feature(*vmaf, hip_state ? "vif_hip" : "vif", opts);
    }
    return err;
}

/* NUM_FRAMES frames through one extractor, and every key of the case of every
 * frame read into `out` (frame-major). Returns the first error; -ENOSYS is
 * the scaffold build. */
static int vif_scores(VmafHipState *hip_state, const VifCase *c, double *out)
{
    VmafContext *vmaf = NULL;
    int err = vif_context(&vmaf, hip_state, c);
    for (unsigned frame = 0; frame < NUM_FRAMES && !err; frame++) {
        err = feed_frame(vmaf, c->bpc, frame);
    }
    if (!err) {
        err = vmaf_read_pictures(vmaf, NULL, NULL, 0);
    }
    for (size_t i = 0; i < c->n_keys * NUM_FRAMES && !err; i++) {
        const char *key = c->keys[i % c->n_keys];
        err = vmaf_feature_score_at_index(vmaf, key, &out[i], (unsigned)(i / c->n_keys));
        if (err) {
            (void)fprintf(stderr, "\n%s: no score for %s (%s)\n", c->name, key,
                          hip_state ? "HIP" : "CPU");
        }
    }
    const int closed = vmaf ? vmaf_close(vmaf) : 0;
    return err ? err : closed;
}

/* The device, or NULL with the reason printed when there is none. */
static VmafHipState *hip_device(void)
{
    VmafHipState *hip_state = NULL;
    const VmafHipConfiguration hip_cfg = {.device_index = -1};
    if (vmaf_hip_state_init(&hip_state, hip_cfg) != 0 || hip_state == NULL) {
        (void)fprintf(stderr, "[skip: no HIP device] ");
        mu_skipped = 1;
        return NULL;
    }
    return hip_state;
}

/* The outputs of the case whose HIP value is not the CPU's, each one
 * reported; UINT32_MAX when a run failed. A skipped HIP leg counts as 0. */
static unsigned exact_mismatches(const VifCase *c)
{
    double cpu[MAX_KEYS * NUM_FRAMES] = {0.0};
    double gpu[MAX_KEYS * NUM_FRAMES] = {0.0};
    VmafHipState *hip_state = hip_device();
    if (!hip_state) {
        return 0u;
    }
    const int gpu_err = vif_scores(hip_state, c, gpu);
    vmaf_hip_state_free(&hip_state);
    if (gpu_err == -ENOSYS) {
        (void)fprintf(stderr, "[skip: HIP kernels not built (enable_hipcc=false)] ");
        mu_skipped = 1;
        return 0u;
    }
    const int cpu_err = gpu_err ? 0 : vif_scores(NULL, c, cpu);
    if (gpu_err || cpu_err) {
        (void)fprintf(stderr, "\n%s: run failed (hip %d, cpu %d)\n", c->name, gpu_err, cpu_err);
        return UINT32_MAX;
    }
    unsigned mismatches = 0u;
    for (size_t i = 0; i < c->n_keys * NUM_FRAMES; i++) {
        if (isfinite(cpu[i]) && cpu[i] == gpu[i]) {
            continue;
        }
        mismatches++;
        (void)fprintf(stderr, "\n%s %ux%u frame %u %s: cpu=%.17g hip=%.17g delta=%.3e\n", c->name,
                      FIXTURE_W, FIXTURE_H, (unsigned)(i / c->n_keys), c->keys[i % c->n_keys],
                      cpu[i], gpu[i], fabs(cpu[i] - gpu[i]));
    }
    return mismatches;
}

static char *test_vif_hip_registered(void)
{
    VmafFeatureExtractor *fex = vmaf_get_feature_extractor_by_name("vif_hip");
    mu_assert("vif_hip extractor must be registered", fex != NULL);
    mu_assert("vif_hip name matches", !strcmp(fex->name, "vif_hip"));
    return NULL;
}

static char *test_vif_default_exact(void)
{
    static const VifCase c = {.name = "default", .bpc = 8u, .n_keys = 4u, .keys = {SCALE_KEYS}};
    mu_assert("vif_hip is not bit-identical to the CPU vif extractor", exact_mismatches(&c) == 0u);
    return NULL;
}

/* debug=true publishes the frame ratio and the sums it is formed from: the
 * accumulators themselves, rounded to float as vif_store_residuals() does. */
static char *test_vif_debug_exact(void)
{
    static const VifCase c = {.name = "debug",
                              .bpc = 8u,
                              .option = "debug",
                              .value = "true",
                              .n_keys = 15u,
                              .keys = {DEBUG_KEYS}};
    mu_assert("vif_hip debug outputs are not the CPU's bit for bit", exact_mismatches(&c) == 0u);
    return NULL;
}

/* 10 and 12 bits go through the 16-bit kernels at scale 0, with the rounding
 * shifts of that bit depth. */
static char *test_vif_10bit_exact(void)
{
    static const VifCase c = {.name = "10-bit",
                              .bpc = 10u,
                              .option = "debug",
                              .value = "true",
                              .n_keys = 15u,
                              .keys = {DEBUG_KEYS}};
    mu_assert("vif_hip is not bit-identical to the CPU at 10 bits", exact_mismatches(&c) == 0u);
    return NULL;
}

static char *test_vif_12bit_exact(void)
{
    static const VifCase c = {.name = "12-bit", .bpc = 12u, .n_keys = 4u, .keys = {SCALE_KEYS}};
    mu_assert("vif_hip is not bit-identical to the CPU at 12 bits", exact_mismatches(&c) == 0u);
    return NULL;
}

/* vif_enhn_gain_limit=1.0 caps the gain at every pixel whose distorted
 * variance exceeds the reference's. */
static char *test_vif_gain_limit_exact(void)
{
    static const VifCase c = {.name = "vif_enhn_gain_limit=1.0",
                              .bpc = 8u,
                              .option = "vif_enhn_gain_limit",
                              .value = "1.0",
                              .n_keys = 4u,
                              .keys = {OPTION_SCALE_KEYS("_egl_1")}};
    mu_assert("vif_hip is not bit-identical to the CPU with vif_enhn_gain_limit=1.0",
              exact_mismatches(&c) == 0u);
    return NULL;
}

/* vif_skip_scale0 publishes 0.0 for scale 0 and the CPU's values for the
 * other three. */
static char *test_vif_skip_scale0_exact(void)
{
    static const VifCase c = {.name = "vif_skip_scale0",
                              .bpc = 8u,
                              .option = "vif_skip_scale0",
                              .value = "true",
                              .n_keys = 4u,
                              .keys = {OPTION_SCALE_KEYS("_ssclz")}};
    mu_assert("vif_hip is not bit-identical to the CPU with vif_skip_scale0",
              exact_mismatches(&c) == 0u);
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_vif_hip_registered);
    mu_run_test(test_vif_default_exact);
    mu_run_test(test_vif_debug_exact);
    mu_run_test(test_vif_10bit_exact);
    mu_run_test(test_vif_12bit_exact);
    mu_run_test(test_vif_gain_limit_exact);
    mu_run_test(test_vif_skip_scale0_exact);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
