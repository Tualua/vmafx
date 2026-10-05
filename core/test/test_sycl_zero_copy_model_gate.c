/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * The SYCL zero-copy path scores what it can and names what it cannot
 * (ADR-1688, widened by ADR-1768), on a SYCL device.
 *
 * vmaf_read_pictures_sycl() reads the planes the caller put into the shared
 * frame and hands the extractors no host picture. Here the caller writes luma
 * only, with vmaf_sycl_upload_plane() (the host form of a luma-only import such
 * as D3D11's); the VA import's chroma is covered by test_sycl_zerocopy_parity.
 * - a CPU extractor (float_psnr) cannot run there: the call returns -ENOTSUP
 *   before the frame is counted, again on a retry, and the context flushes;
 * - a chroma reader on an import that carried no chroma (the default model
 *   vmaf_v1.0.16_3d0h through speed_chroma_sycl, motion_sycl with
 *   motion_add_uv, psnr_sycl with chroma) returns -ENOTSUP naming itself
 *   (vmaf_sycl_require_chroma) instead of scoring stale planes;
 * - vmaf_v0.6.1 and every twin that reads only the shared luma give the CPU's
 *   per-frame scores, float_psnr_sycl included (it read host pictures before
 *   ADR-1766).
 *
 * Skip behaviour: without a SYCL device the test prints
 * "[skip: no SYCL device]" and exits 77.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test.h"

#include "libvmaf/libvmaf.h"
#include "libvmaf/libvmaf_sycl.h"
#include "libvmaf/model.h"
#include "libvmaf/picture.h"
#include "sycl/common.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit; MSVC's C mode has
 * no `nullptr`. ADR-1138. */

#define GATE_W 576u
#define GATE_H 324u
#define GATE_FRAMES 3u

static uint8_t luma(unsigned row, unsigned col, unsigned frame, unsigned salt)
{
    const unsigned mix = (row * 7u + col * 3u + frame * 11u) ^ ((row * col + salt) * 5u);
    return (uint8_t)((mix + salt * 17u) & 0xFFu);
}

static int fill_frame(VmafPicture *pic, unsigned frame, unsigned salt)
{
    const int err = vmaf_picture_alloc(pic, VMAF_PIX_FMT_YUV420P, 8u, GATE_W, GATE_H);
    if (err) {
        return err;
    }
    for (unsigned p = 0u; p < 3u; p++) {
        uint8_t *data = (uint8_t *)pic->data[p];
        for (unsigned row = 0u; row < pic->h[p]; row++) {
            for (unsigned col = 0u; col < pic->w[p]; col++) {
                data[(size_t)row * (size_t)pic->stride[p] + col] = luma(row, col, frame, salt + p);
            }
        }
    }
    return 0;
}

typedef struct Gate {
    VmafSyclState *state;
    VmafContext *vmaf;
    VmafModel *model;
} Gate;

static int open_state(VmafSyclState **state)
{
    const VmafSyclConfiguration cfg = {.device_index = -1};
    if (vmaf_sycl_state_init(state, cfg) != 0 || *state == NULL) {
        (void)fprintf(stderr, "[skip: no SYCL device] ");
        mu_skipped = 1;
        return -ENODEV;
    }
    return 0;
}

/* A zero-copy context on `state` with `model` (or none) and `feature` (or none,
 * with option key=val when key is set). */
static int open_gate(Gate *g, VmafSyclState *state, const char *model, const char *feature,
                     const char *key, const char *val)
{
    memset(g, 0, sizeof(*g));
    g->state = state;
    const VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_ERROR};
    int err = vmaf_init(&g->vmaf, cfg);
    err = err ? err : vmaf_sycl_import_state(g->vmaf, state);
    err = err ? err : vmaf_sycl_init_frame_buffers(g->vmaf, GATE_W, GATE_H, 8u);
    if (!err && model) {
        VmafModelConfig mcfg = {.name = "vmaf", .flags = VMAF_MODEL_FLAGS_DEFAULT};
        err = vmaf_model_load(&g->model, &mcfg, model);
        err = err ? err : vmaf_use_features_from_model(g->vmaf, g->model);
    }
    if (!err && feature) {
        VmafFeatureDictionary *opts = NULL;
        if (key) {
            err = vmaf_feature_dictionary_set(&opts, key, val);
        }
        err = err ? err : vmaf_use_feature(g->vmaf, feature, opts);
    }
    return err;
}

static int close_gate(Gate *g)
{
    const int err = g->vmaf ? vmaf_close(g->vmaf) : 0;
    if (g->model) {
        vmaf_model_destroy(g->model);
    }
    return err;
}

/* Put frame `frame`'s luma into the shared frame and read it. */
static int read_zero_copy(Gate *g, unsigned frame)
{
    VmafPicture ref;
    VmafPicture dist;
    int err = fill_frame(&ref, frame, 0u);
    err = err ? err : fill_frame(&dist, frame, 9u);
    if (err) {
        return err;
    }
    err = vmaf_sycl_upload_plane(g->state, ref.data[0], (unsigned)ref.stride[0], 1, GATE_W, GATE_H,
                                 8u);
    err = err ? err :
                vmaf_sycl_upload_plane(g->state, dist.data[0], (unsigned)dist.stride[0], 0, GATE_W,
                                       GATE_H, 8u);
    /* vmaf_sycl_upload_plane() enqueues on the copy queue; wait for it here
     * so the test measures the admission, not the upload ordering. */
    err = err ? err : vmaf_sycl_wait_copy_queue(g->state);
    err = err ? err : vmaf_read_pictures_sycl(g->vmaf, frame);
    const int unref_ref = vmaf_picture_unref(&ref);
    const int unref_dist = vmaf_picture_unref(&dist);
    if (err) {
        return err;
    }
    return unref_ref ? unref_ref : unref_dist;
}

/* The first frame is refused with -ENOTSUP, again on a retry, and the
 * context still flushes and closes. */
static char *refused(VmafSyclState *state, const char *model, const char *feature, const char *key,
                     const char *val)
{
    Gate g;
    mu_assert("zero-copy context opens", open_gate(&g, state, model, feature, key, val) == 0);
    mu_assert("first frame refused with -ENOTSUP", read_zero_copy(&g, 0u) == -ENOTSUP);
    mu_assert("a retry is refused the same way", read_zero_copy(&g, 0u) == -ENOTSUP);
    mu_assert("the refused context flushes without error", vmaf_flush_sycl(g.vmaf) == 0);
    mu_assert("the refused context closes", close_gate(&g) == 0);
    return NULL;
}

/* A chroma reader on luma-only input is refused at its submit(), after the
 * admission check, so only the first call's status is pinned; the context
 * still closes. */
static char *refused_without_chroma(VmafSyclState *state, const char *model, const char *feature,
                                    const char *key, const char *val)
{
    Gate g;
    mu_assert("zero-copy context opens", open_gate(&g, state, model, feature, key, val) == 0);
    mu_assert("first frame refused with -ENOTSUP", read_zero_copy(&g, 0u) == -ENOTSUP);
    (void)vmaf_flush_sycl(g.vmaf);
    mu_assert("the refused context closes", close_gate(&g) == 0);
    return NULL;
}

static char *test_chroma_readers_without_chroma_refused(void)
{
    VmafSyclState *state = NULL;
    if (open_state(&state)) {
        return NULL;
    }
    char *msg = refused_without_chroma(state, "vmaf_v1.0.16_3d0h", NULL, NULL, NULL);
    msg = msg ? msg : refused_without_chroma(state, NULL, "motion_sycl", "motion_add_uv", "true");
    msg = msg ? msg : refused_without_chroma(state, NULL, "psnr_sycl", NULL, NULL);
    vmaf_sycl_state_free(&state);
    return msg;
}

static char *test_cpu_extractor_refused(void)
{
    VmafSyclState *state = NULL;
    if (open_state(&state)) {
        return NULL;
    }
    char *msg = refused(state, NULL, "float_psnr", NULL, NULL);
    vmaf_sycl_state_free(&state);
    return msg;
}

/* What a context scores: a model (its VMAF), or one feature extractor with
 * an optional option and the collector key read back. */
typedef struct Spec {
    const char *model;
    const char *feature, *key, *val;
    const char *score;
} Spec;

static int open_plain(VmafContext **vmaf, VmafModel **model, const Spec *spec)
{
    const VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_ERROR};
    VmafModelConfig mcfg = {.name = "vmaf", .flags = VMAF_MODEL_FLAGS_DEFAULT};
    int err = vmaf_init(vmaf, cfg);
    if (!err && spec->model) {
        err = vmaf_model_load(model, &mcfg, spec->model);
        err = err ? err : vmaf_use_features_from_model(*vmaf, *model);
    }
    if (!err && spec->feature) {
        VmafFeatureDictionary *opts = NULL;
        if (spec->key) {
            err = vmaf_feature_dictionary_set(&opts, spec->key, spec->val);
        }
        err = err ? err : vmaf_use_feature(*vmaf, spec->feature, opts);
    }
    return err;
}

static int read_scores(VmafContext *vmaf, VmafModel *model, const Spec *spec, double *out)
{
    int err = 0;
    for (unsigned f = 0u; !err && f < GATE_FRAMES; f++) {
        err = model ? vmaf_score_at_index(vmaf, model, &out[f], f) :
                      vmaf_feature_score_at_index(vmaf, spec->score, &out[f], f);
    }
    return err;
}

/* `spec` from host frames, with the CPU extractors. */
static int cpu_scores(const Spec *spec, double *out)
{
    VmafContext *vmaf = NULL;
    VmafModel *model = NULL;
    int err = open_plain(&vmaf, &model, spec);
    for (unsigned f = 0u; !err && f < GATE_FRAMES; f++) {
        VmafPicture ref;
        VmafPicture dist;
        err = fill_frame(&ref, f, 0u);
        err = err ? err : fill_frame(&dist, f, 9u);
        err = err ? err : vmaf_read_pictures(vmaf, &ref, &dist, f);
    }
    err = err ? err : vmaf_read_pictures(vmaf, NULL, NULL, 0);
    err = err ? err : read_scores(vmaf, model, spec, out);
    if (model) {
        vmaf_model_destroy(model);
    }
    const int close_err = vmaf ? vmaf_close(vmaf) : 0;
    return err ? err : close_err;
}

/* `spec` (with SYCL extractor names) on the zero-copy path. */
static int zero_copy_scores(VmafSyclState *state, const Spec *spec, double *out)
{
    Gate g;
    int err = open_gate(&g, state, spec->model, spec->feature, spec->key, spec->val);
    for (unsigned f = 0u; !err && f < GATE_FRAMES; f++) {
        err = read_zero_copy(&g, f);
    }
    err = err ? err : vmaf_flush_sycl(g.vmaf);
    err = err ? err : read_scores(g.vmaf, g.model, spec, out);
    const int close_err = close_gate(&g);
    return err ? err : close_err;
}

/* The zero-copy scores of `zc` equal the CPU scores of `cpu`, frame by frame. */
static int same_as_cpu(VmafSyclState *state, const Spec *zc, const Spec *cpu)
{
    double a[GATE_FRAMES] = {0};
    double b[GATE_FRAMES] = {0};
    if (zero_copy_scores(state, zc, a) != 0 || cpu_scores(cpu, b) != 0) {
        (void)fprintf(stderr, "%s: a run failed\n", zc->model ? zc->model : zc->feature);
        return 0;
    }
    int same = 1;
    for (unsigned f = 0u; f < GATE_FRAMES; f++) {
        (void)fprintf(stderr, "%s frame %u: cpu %.17g zero-copy %.17g\n",
                      zc->model ? zc->model : zc->feature, f, b[f], a[f]);
        same = same && (a[f] == b[f]);
    }
    return same;
}

static char *test_luma_model_runs_and_matches_cpu(void)
{
    VmafSyclState *state = NULL;
    if (open_state(&state)) {
        return NULL;
    }
    const Spec model = {.model = "vmaf_v0.6.1"};
    const int same = same_as_cpu(state, &model, &model);
    vmaf_sycl_state_free(&state);
    mu_assert("vmaf_v0.6.1 on the zero-copy path is the CPU's on every frame", same);
    return NULL;
}

/* Every other admitted twin runs on the zero-copy path and gives the CPU's
 * value: the hook's true is checked, not only its false. */
static char *test_admitted_twins_run_and_match_cpu(void)
{
    static const Spec cases[][2] = {
        {{.feature = "psnr_sycl", .key = "enable_chroma", .val = "false", .score = "psnr_y"},
         {.feature = "psnr", .key = "enable_chroma", .val = "false", .score = "psnr_y"}},
        {{.feature = "psnr_hvs_sycl", .key = "enable_chroma", .val = "false", .score = "psnr_hvs"},
         {.feature = "psnr_hvs", .key = "enable_chroma", .val = "false", .score = "psnr_hvs"}},
        {{.feature = "float_moment_sycl", .score = "float_moment_ref1st"},
         {.feature = "float_moment", .score = "float_moment_ref1st"}},
        {{.feature = "motion_v2_sycl", .score = "VMAF_integer_feature_motion_v2_sad_score"},
         {.feature = "motion_v2", .score = "VMAF_integer_feature_motion_v2_sad_score"}},
        {{.feature = "cambi_sycl", .score = "Cambi_feature_cambi_score"},
         {.feature = "cambi", .score = "Cambi_feature_cambi_score"}},
        {{.feature = "float_psnr_sycl", .score = "float_psnr"},
         {.feature = "float_psnr", .score = "float_psnr"}},
    };
    VmafSyclState *state = NULL;
    if (open_state(&state)) {
        return NULL;
    }
    int same = 1;
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        same = same_as_cpu(state, &cases[i][0], &cases[i][1]) && same;
    }
    vmaf_sycl_state_free(&state);
    mu_assert("an admitted twin on the zero-copy path is not the CPU's", same);
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_chroma_readers_without_chroma_refused);
    mu_run_test(test_cpu_extractor_refused);
    mu_run_test(test_luma_model_runs_and_matches_cpu);
    mu_run_test(test_admitted_twins_run_and_match_cpu);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
