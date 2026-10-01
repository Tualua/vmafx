/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * The first frame of a HIP context is right when the process has had a
 * smaller context before (ADR-1427,
 * T-HIP-FIRST-FRAME-ASYNC-CLEAR-OTHER-TWINS-2026-10-01).
 *
 * Several HIP twins add into accumulators on the device and clear them with
 * an asynchronous memset every frame. On a gfx1036 a clear queued ahead of
 * the frame's plane upload has no effect in the first context of a process
 * that needs larger planes than the contexts before it: the accumulators
 * then start from what recycled device memory holds, the sums of the smaller
 * context, and the first frame is wrong. `adm_hip` returned a NaN numerator
 * that way, `float_moment_hip` moments 3 % too large, `vif_hip` scores 0.02
 * too low. The `vmaf` tool has one context per process and fresh device
 * memory is zero, so it never shows.
 *
 * For a twin: one frame in a 640x360 context, then one frame in a 3840x2160
 * context. The smaller context's distorted picture has little left of its
 * reference, so sums it leaves behind move every score of the larger one,
 * the ratios too. The first frame of the larger context must be the CPU's
 * within the twin's parity tolerance. Only the first larger context of a
 * process is exposed, so every twin has its own binary: meson builds this
 * file once per twin of the table, with the twin's name in FIRST_FRAME_TWIN.
 *
 * The motion twins are not here: their first frame has no score to compare.
 *
 * Skip behaviour: without a HIP device, or on a build without the device
 * kernels (-ENOSYS), the test reports the skip and passes.
 */

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test.h"

#include "libvmaf/libvmaf.h"
#include "libvmaf/libvmaf_hip.h"
#include "libvmaf/picture.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy also proposes the `nullptr` keyword, but MSVC's
 * documented /std:clatest C23 feature set does not include `nullptr` while the
 * required Windows build compiles this TU with cl.exe, and this file mirrors
 * the C spelling of the surface it exercises. ADR-1138. */

#ifndef FIRST_FRAME_TWIN
#error "build with -DFIRST_FRAME_TWIN=\"<hip extractor>\" (core/test/meson.build)"
#endif

#define MAX_KEYS 5u
#define SMALL_W 640u
#define SMALL_H 360u
#define LARGE_W 3840u
#define LARGE_H 2160u

typedef struct FirstFrameCase {
    const char *hip;     /* HIP extractor */
    const char *cpu;     /* its CPU twin */
    const char *opt_key; /* option both need, or NULL */
    const char *opt_val;
    double tol;                 /* the twin's parity tolerance */
    const char *keys[MAX_KEYS]; /* features compared, NULL-terminated */
} FirstFrameCase;

static const FirstFrameCase cases[] = {
    {"adm_hip",
     "adm",
     NULL,
     NULL,
     1e-4,
     {"VMAF_integer_feature_adm2_score", "integer_adm_scale0", "integer_adm_scale1",
      "integer_adm_scale2", "integer_adm_scale3"}},
    {"float_moment_hip",
     "float_moment",
     NULL,
     NULL,
     1e-4,
     {"float_moment_ref1st", "float_moment_dis1st", "float_moment_ref2nd", "float_moment_dis2nd"}},
    {"psnr_hip", "psnr", NULL, NULL, 1e-4, {"psnr_y", "psnr_cb", "psnr_cr"}},
    {"float_psnr_hip", "float_psnr", NULL, NULL, 1e-4, {"float_psnr"}},
    {"vif_hip",
     "vif",
     NULL,
     NULL,
     1e-4,
     {"VMAF_integer_feature_vif_scale0_score", "VMAF_integer_feature_vif_scale1_score",
      "VMAF_integer_feature_vif_scale2_score", "VMAF_integer_feature_vif_scale3_score"}},
    {"float_vif_hip",
     "float_vif",
     NULL,
     NULL,
     1e-4,
     {"VMAF_feature_vif_scale0_score", "VMAF_feature_vif_scale1_score",
      "VMAF_feature_vif_scale2_score", "VMAF_feature_vif_scale3_score"}},
    {"float_adm_hip", "float_adm", NULL, NULL, 1e-4, {"VMAF_feature_adm2_score"}},
    {"ciede_hip", "ciede", NULL, NULL, 1e-4, {"ciede2000"}},
    {"integer_ssim_hip", "ssim", NULL, NULL, 1e-4, {"ssim"}},
    {"float_ssim_hip", "float_ssim", "scale", "1", 1e-3, {"float_ssim"}},
    {"integer_ms_ssim_hip", "float_ms_ssim", NULL, NULL, 1e-4, {"float_ms_ssim"}},
    {"psnr_hvs_hip", "psnr_hvs", NULL, NULL, 1e-4, {"psnr_hvs"}},
    {"cambi_hip", "cambi", NULL, NULL, 1e-4, {"Cambi_feature_cambi_score"}},
    {"ssimulacra2_hip", "ssimulacra2", NULL, NULL, 1e-4, {"ssimulacra2"}},
};
#define N_CASES (sizeof(cases) / sizeof(cases[0]))

/* What a frame is: the reference, a dimmed and blocked copy of it, or a
 * copy that has little left of it. */
typedef enum Content { CONTENT_REFERENCE, CONTENT_MILD, CONTENT_HEAVY } Content;

/* The distorted sample of reference sample `v` at (`col`, `row`). */
static unsigned distort(unsigned v, unsigned col, unsigned row, Content content)
{
    if (content == CONTENT_MILD) {
        return v - (v / 9u) + ((((col / 8u) ^ (row / 8u)) & 1u) * 6u);
    }
    if (content == CONTENT_HEAVY) {
        return (((col / 16u) + (row / 16u)) & 1u) ? 255u - v : v / 4u;
    }
    return v;
}

/* A textured frame, or a distorted copy of it. */
static int fill_picture(VmafPicture *pic, unsigned w, unsigned h, Content content)
{
    const int err = vmaf_picture_alloc(pic, VMAF_PIX_FMT_YUV420P, 8u, w, h);
    if (err) {
        return err;
    }
    uint32_t state = 0x9E3779B9u ^ (w * 2654435761u);
    for (unsigned p = 0; p < 3u; p++) {
        uint8_t *plane = (uint8_t *)pic->data[p];
        for (unsigned row = 0; row < pic->h[p]; row++) {
            for (unsigned col = 0; col < pic->w[p]; col++) {
                state = (state * 1664525u) + 1013904223u;
                const unsigned v =
                    ((((col * 5u) + (row * 3u)) & 127u) + 48u) + ((state >> 8) & 31u);
                plane[((size_t)row * (size_t)pic->stride[p]) + col] =
                    (uint8_t)distort(v, col, row, content);
            }
        }
    }
    return 0;
}

/* One frame of `w` x `h` through `vmaf`, which takes both pictures. */
static int feed_one_frame(VmafContext *vmaf, unsigned w, unsigned h, Content distortion)
{
    VmafPicture ref;
    VmafPicture dist;
    int err = fill_picture(&ref, w, h, CONTENT_REFERENCE);
    if (err) {
        return err;
    }
    err = fill_picture(&dist, w, h, distortion);
    if (err) {
        (void)vmaf_picture_unref(&ref);
        return err;
    }
    return vmaf_read_pictures(vmaf, &ref, &dist, 0u);
}

/* A context with the case's CPU extractor, or its twin on `hip_state`. */
static int case_context(VmafContext **vmaf, const FirstFrameCase *c, VmafHipState *hip_state)
{
    const VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    VmafFeatureDictionary *opts = NULL;
    int err = vmaf_init(vmaf, cfg);
    if (!err && hip_state) {
        err = vmaf_hip_import_state(*vmaf, hip_state);
    }
    if (!err && c->opt_key) {
        err = vmaf_feature_dictionary_set(&opts, c->opt_key, c->opt_val);
    }
    if (!err) {
        err = vmaf_use_feature(*vmaf, hip_state ? c->hip : c->cpu, opts);
        opts = err ? opts : NULL; /* taken on success */
    }
    if (opts) {
        (void)vmaf_feature_dictionary_free(&opts);
    }
    return err;
}

/* A frame size and how its distorted picture is made. */
typedef struct Frame {
    unsigned w;
    unsigned h;
    Content distortion;
} Frame;

/* The case's keys of the first frame of a context fed `f`. */
static int first_frame_scores(const FirstFrameCase *c, VmafHipState *hip_state, Frame f,
                              double *out)
{
    VmafContext *vmaf = NULL;
    int err = case_context(&vmaf, c, hip_state);
    if (!err) {
        err = feed_one_frame(vmaf, f.w, f.h, f.distortion);
    }
    if (!err) {
        err = vmaf_read_pictures(vmaf, NULL, NULL, 0);
    }
    for (unsigned k = 0; k < MAX_KEYS && c->keys[k] != NULL && !err; k++) {
        err = vmaf_feature_score_at_index(vmaf, c->keys[k], &out[k], 0u);
    }
    const int closed = vmaf ? vmaf_close(vmaf) : 0;
    return err ? err : closed;
}

/* A context on a fresh HIP state, as a second program run would make it. */
static int hip_first_frame(const FirstFrameCase *c, Frame f, double *out)
{
    VmafHipState *hip_state = NULL;
    const VmafHipConfiguration hip_cfg = {.device_index = -1};
    if (vmaf_hip_state_init(&hip_state, hip_cfg) != 0 || hip_state == NULL) {
        return -ENODEV;
    }
    const int err = first_frame_scores(c, hip_state, f, out);
    vmaf_hip_state_free(&hip_state);
    return err;
}

/* 0 when the first frame of a 3840x2160 context is the CPU's within the
 * tolerance, 1 when it is not (reported), a negative errno when a run
 * failed (reported). */
static int check_larger_context(const FirstFrameCase *c)
{
    const Frame frame = {LARGE_W, LARGE_H, CONTENT_MILD};
    double gpu[MAX_KEYS] = {0.0};
    double cpu[MAX_KEYS] = {0.0};
    const char *stage = "the larger HIP context";
    int err = hip_first_frame(c, frame, gpu);
    if (!err) {
        stage = "the CPU extractor";
        err = first_frame_scores(c, NULL, frame, cpu);
    }
    if (err) {
        (void)fprintf(stderr, "\n%s %ux%u: %s failed (%d)", c->hip, frame.w, frame.h, stage, err);
        return err;
    }
    int off = 0;
    for (unsigned k = 0; k < MAX_KEYS && c->keys[k] != NULL; k++) {
        const double delta = fabs(cpu[k] - gpu[k]);
        if (delta <= c->tol) {
            continue;
        }
        off = 1;
        (void)fprintf(stderr, "\n%s %ux%u %s: cpu=%.17g hip=%.17g delta=%.3e", c->hip, frame.w,
                      frame.h, c->keys[k], cpu[k], gpu[k], delta);
    }
    return off;
}

/* The case this binary was built for, or NULL when the table has no such
 * twin. */
static const FirstFrameCase *built_case(void)
{
    for (size_t i = 0; i < N_CASES; i++) {
        if (strcmp(FIRST_FRAME_TWIN, cases[i].hip) == 0) {
            return &cases[i];
        }
    }
    return NULL;
}

static char *test_first_frame_after_a_smaller_context(void)
{
    const FirstFrameCase *c = built_case();
    mu_assert("FIRST_FRAME_TWIN names no HIP twin of this test", c != NULL);
    (void)fprintf(stderr, "[%s] ", c->hip);
    /* Heavily distorted in the smaller context: accumulators it leaves
     * behind then move every score of the larger one, the ratios too. */
    const Frame before = {SMALL_W, SMALL_H, CONTENT_HEAVY};
    double small[MAX_KEYS] = {0.0};
    const int smaller = hip_first_frame(c, before, small);
    if (smaller == -ENODEV || smaller == -ENOSYS) {
        (void)fprintf(stderr, "[skip: no HIP device, or the HIP extractors are scaffolds] ");
        mu_skipped = 1;
        return NULL;
    }
    mu_assert("the smaller HIP context failed", smaller == 0);
    mu_assert("the twin's first frame in a later, larger context is not the CPU's",
              check_larger_context(c) == 0);
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_first_frame_after_a_smaller_context);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
