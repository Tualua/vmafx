/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * Zero-copy chroma currency and parity (ADR-1597, phase 12 plan 07).
 *
 * The VA import path writes the upload slots and then advances the frame;
 * it never goes through a host picture. This test emulates it (upload-slot
 * writes, vmaf_sycl_shared_chroma_mark_imported, vmaf_read_pictures_sycl) so
 * the runtime contract can be checked without a VA device:
 *
 *  - vmaf_sycl_init_frame_buffers() allocates the shared chroma planes before
 *    frame 0 (D-01);
 *  - chroma is current for a frame only after an import marked it and
 *    vmaf_sycl_advance_frame() promoted it, never stale;
 *  - psnr_sycl / psnr_hvs_sycl on emulated zero-copy equal the CPU extractors
 *    frame by frame, frame 0 included;
 *  - without a mark the chroma readers fail with -ENOTSUP;
 *  - motion_sycl with motion_add_uv=true on emulated zero-copy equals the same
 *    extractor on host-uploaded pictures, bit for bit, over 6 frames with
 *    changing chroma (the CPU integer `motion` has no motion_add_uv option, so
 *    the host-upload leg of the same twin is the reference; plan 12-08).
 *
 * Skip behaviour: without a SYCL device the test prints
 * "[skip: no SYCL device]" and passes, like test_sycl_shared_planes.c.
 */

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test.h"

#include "libvmaf/feature.h"
#include "libvmaf/libvmaf.h"
#include "libvmaf/libvmaf_sycl.h"
#include "libvmaf/picture.h"
#include "sycl/common.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy also proposes the `nullptr` keyword, but this is a C
 * translation unit whose sources spell the null pointer constant `NULL` and
 * MSVC's documented /std:clatest C23 feature set does not include `nullptr`
 * while the required Windows build compiles this TU with cl.exe. ADR-1138. */

/* Odd luma size: 4:2:0 chroma is 34 x 19. */
#define FRAME_W 67u
#define FRAME_H 37u
#define CHROMA_W ((FRAME_W + 1u) / 2u)
#define CHROMA_H ((FRAME_H + 1u) / 2u)
#define PARITY_FRAMES 5u
#define EXACT_TOL 1e-9
#define HVS_TOL 1e-4

static VmafSyclState *open_state(void)
{
    VmafSyclState *state = NULL;
    VmafSyclConfiguration cfg = {.device_index = -1};
    if (vmaf_sycl_state_init(&state, cfg) != 0 || !state) {
        (void)fprintf(stderr, "[skip: no SYCL device] ");
        mu_skipped = 1;
        return NULL;
    }
    return state;
}

static char *open_context(VmafSyclState *state, unsigned bpc, VmafContext **vmaf)
{
    VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    mu_assert("vmaf_init failed", !vmaf_init(vmaf, cfg));
    mu_assert("vmaf_sycl_import_state failed", !vmaf_sycl_import_state(*vmaf, state));
    mu_assert("vmaf_sycl_init_frame_buffers failed",
              !vmaf_sycl_init_frame_buffers(*vmaf, FRAME_W, FRAME_H, bpc));
    return NULL;
}

static char *check_eager_planes(VmafSyclState *state)
{
    for (int is_ref = 0; is_ref < 2; is_ref++) {
        for (unsigned plane = 0; plane < 3u; plane++) {
            mu_assert("upload slot plane must exist before frame 0",
                      vmaf_sycl_get_shared_plane_upload(state, is_ref, plane) != NULL);
        }
        mu_assert("plane 3 does not exist",
                  vmaf_sycl_get_shared_plane_upload(state, is_ref, 3u) == NULL);
    }
    mu_assert("Cb and Cr are different planes",
              vmaf_sycl_get_shared_plane_upload(state, 1, 1u) !=
                  vmaf_sycl_get_shared_plane_upload(state, 1, 2u));
    mu_assert("NULL state yields NULL", vmaf_sycl_get_shared_plane_upload(NULL, 1, 1u) == NULL);
    return NULL;
}

static char *test_eager_chroma_allocation(void)
{
    VmafSyclState *state = open_state();
    if (!state)
        return NULL;
    VmafContext *vmaf = NULL;
    char *msg = open_context(state, 8u, &vmaf);
    if (!msg)
        msg = check_eager_planes(state);
    /* Extractor-init calls with the same geometry stay idempotent. */
    if (!msg && vmaf_sycl_shared_chroma_init(state, CHROMA_W, CHROMA_H))
        msg = "chroma init with the eager geometry must stay idempotent";
    if (vmaf)
        (void)vmaf_close(vmaf);
    vmaf_sycl_state_free(&state);
    return msg;
}

static char *check_currency_promotion(VmafSyclState *state, const void *up_ref, const void *up_dis);

static char *check_currency(VmafSyclState *state)
{
    mu_assert("chroma is not current before any import", !vmaf_sycl_shared_chroma_current(state));
    /* Both slots start at 0; one advance separates upload from compute. */
    vmaf_sycl_advance_frame(state);
    mu_assert("an advance without any mark leaves chroma stale",
              !vmaf_sycl_shared_chroma_current(state));

    const void *const up_ref = vmaf_sycl_get_shared_plane_upload(state, 1, 1u);
    const void *const up_dis = vmaf_sycl_get_shared_plane_upload(state, 0, 2u);
    mu_assert("upload slot differs from the compute slot",
              up_ref != vmaf_sycl_get_shared_plane(state, 1, 1u) &&
                  up_dis != vmaf_sycl_get_shared_plane(state, 0, 2u));
    mu_assert("plane 0 upload slot is the luma upload slot",
              vmaf_sycl_get_shared_plane_upload(state, 1, 0u) ==
                  vmaf_sycl_get_shared_ref_upload(state));

    return check_currency_promotion(state, up_ref, up_dis);
}

/* The mark, the advance that promotes it, and the advance that retires it. */
static char *check_currency_promotion(VmafSyclState *state, const void *up_ref, const void *up_dis)
{
    vmaf_sycl_shared_chroma_mark_imported(state);
    mu_assert("a mark alone does not make chroma current", !vmaf_sycl_shared_chroma_current(state));
    vmaf_sycl_advance_frame(state);
    mu_assert("mark + advance makes chroma current", vmaf_sycl_shared_chroma_current(state));
    mu_assert("the upload slot became the compute slot",
              vmaf_sycl_get_shared_plane(state, 1, 1u) == up_ref &&
                  vmaf_sycl_get_shared_plane(state, 0, 2u) == up_dis);

    vmaf_sycl_advance_frame(state);
    mu_assert("an advance without a mark leaves chroma stale",
              !vmaf_sycl_shared_chroma_current(state));
    return NULL;
}

static char *check_require_chroma(VmafSyclState *state)
{
    VmafPicture pic;
    memset(&pic, 0, sizeof(pic));
    mu_assert("stale chroma without pictures must be refused",
              vmaf_sycl_require_chroma(state, "x", NULL, NULL) == -ENOTSUP);
    mu_assert("one missing picture must be refused",
              vmaf_sycl_require_chroma(state, "x", &pic, NULL) == -ENOTSUP);
    mu_assert("host pictures need no chroma mark",
              vmaf_sycl_require_chroma(state, "x", &pic, &pic) == 0);
    vmaf_sycl_shared_chroma_mark_imported(state);
    vmaf_sycl_advance_frame(state);
    mu_assert("current chroma is accepted", vmaf_sycl_require_chroma(state, "x", NULL, NULL) == 0);
    return NULL;
}

static char *test_chroma_currency_contract(void)
{
    VmafSyclState *state = open_state();
    if (!state)
        return NULL;
    VmafContext *vmaf = NULL;
    char *msg = open_context(state, 8u, &vmaf);
    if (!msg)
        msg = check_currency(state);
    if (!msg)
        msg = check_require_chroma(state);
    if (vmaf)
        (void)vmaf_close(vmaf);
    vmaf_sycl_state_free(&state);
    return msg;
}

/* ------------------------------------------------------------------ */
/* Parity vs CPU on emulated zero-copy                                 */
/* ------------------------------------------------------------------ */

static const char *const g_features[] = {"psnr_y",      "psnr_cb",     "psnr_cr", "psnr_hvs_y",
                                         "psnr_hvs_cb", "psnr_hvs_cr", "psnr_hvs"};
#define N_FEATURES (sizeof(g_features) / sizeof(g_features[0]))

/* One sample of a plane. Cb and Cr differ (plane enters the mix), ref and dis
 * differ (salt), and every frame differs. */
static unsigned sample(unsigned plane, unsigned row, unsigned col, unsigned frame, unsigned salt,
                       unsigned bpc)
{
    const unsigned mix = (row * (3u + plane)) ^ (col * (5u + salt)) ^ (frame * 37u + plane * 11u);
    const unsigned wide = (row * col * (plane + 1u)) + (salt * 29u) + (frame * 13u);
    return (mix + wide) & ((1u << bpc) - 1u);
}

/* Little-endian packed plane (w x h samples), as the import writes it. */
static void fill_plane(uint8_t *buf, unsigned plane, unsigned w, unsigned h, unsigned frame,
                       unsigned salt, unsigned bpc)
{
    const size_t bps = bpc > 8u ? 2u : 1u;
    for (unsigned row = 0; row < h; row++) {
        for (unsigned col = 0; col < w; col++) {
            const unsigned v = sample(plane, row, col, frame, salt, bpc);
            uint8_t *at = buf + (((size_t)row * w + col) * bps);
            at[0] = (uint8_t)(v & 0xFFu);
            if (bps == 2u)
                at[1] = (uint8_t)(v >> 8);
        }
    }
}

/* Emulated VA import of one frame: luma and chroma into the upload slots,
 * chroma marked when `mark` is set. */
static int emulate_import(VmafSyclState *state, unsigned frame, unsigned bpc, int mark)
{
    const size_t bps = bpc > 8u ? 2u : 1u;
    uint8_t *buf = malloc((size_t)FRAME_W * FRAME_H * bps);
    if (!buf)
        return -ENOMEM;
    int err = 0;
    for (unsigned is_ref = 0; is_ref < 2u && !err; is_ref++) {
        const unsigned salt = is_ref ? 0u : 1u;
        fill_plane(buf, 0u, FRAME_W, FRAME_H, frame, salt, bpc);
        err = vmaf_sycl_upload_plane(state, buf, FRAME_W * (unsigned)bps, (int)is_ref, FRAME_W,
                                     FRAME_H, bpc);
        for (unsigned plane = 1u; plane < 3u && !err; plane++) {
            fill_plane(buf, plane, CHROMA_W, CHROMA_H, frame, salt, bpc);
            err = vmaf_sycl_memcpy_h2d(state,
                                       vmaf_sycl_get_shared_plane_upload(state, (int)is_ref, plane),
                                       buf, (size_t)CHROMA_W * CHROMA_H * bps);
        }
    }
    free(buf);
    if (!err && mark)
        vmaf_sycl_shared_chroma_mark_imported(state);
    return err;
}

static int fill_pic(VmafPicture *pic, unsigned frame, unsigned salt, unsigned bpc)
{
    const int err = vmaf_picture_alloc(pic, VMAF_PIX_FMT_YUV420P, bpc, FRAME_W, FRAME_H);
    if (err)
        return err;
    const size_t bps = bpc > 8u ? 2u : 1u;
    uint8_t *packed = malloc((size_t)FRAME_W * FRAME_H * bps);
    if (!packed)
        return -ENOMEM;
    for (unsigned plane = 0; plane < 3u; plane++) {
        fill_plane(packed, plane, pic->w[plane], pic->h[plane], frame, salt, bpc);
        for (unsigned row = 0; row < pic->h[plane]; row++) {
            memcpy((uint8_t *)pic->data[plane] + ((size_t)row * pic->stride[plane]),
                   packed + ((size_t)row * pic->w[plane] * bps), (size_t)pic->w[plane] * bps);
        }
    }
    free(packed);
    return 0;
}

static char *use_features(VmafContext *vmaf, int sycl)
{
    const char *const names[2] = {sycl ? "psnr_sycl" : "psnr", sycl ? "psnr_hvs_sycl" : "psnr_hvs"};
    for (unsigned i = 0; i < 2u; i++)
        mu_assert("vmaf_use_feature failed", !vmaf_use_feature(vmaf, names[i], NULL));
    return NULL;
}

static char *read_scores(VmafContext *vmaf, double scores[N_FEATURES][PARITY_FRAMES])
{
    for (unsigned f = 0; f < N_FEATURES; f++) {
        for (unsigned frame = 0; frame < PARITY_FRAMES; frame++) {
            mu_assert("score missing",
                      !vmaf_feature_score_at_index(vmaf, g_features[f], &scores[f][frame], frame));
        }
    }
    return NULL;
}

static char *read_cpu_frames(VmafContext *vmaf, unsigned bpc)
{
    for (unsigned frame = 0; frame < PARITY_FRAMES; frame++) {
        VmafPicture ref;
        VmafPicture dis;
        mu_assert("ref alloc", !fill_pic(&ref, frame, 0u, bpc));
        mu_assert("dis alloc", !fill_pic(&dis, frame, 1u, bpc));
        mu_assert("cpu read failed", !vmaf_read_pictures(vmaf, &ref, &dis, frame));
    }
    return NULL;
}

static char *run_cpu(unsigned bpc, double scores[N_FEATURES][PARITY_FRAMES])
{
    VmafContext *vmaf = NULL;
    VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    mu_assert("vmaf_init failed", !vmaf_init(&vmaf, cfg));
    mu_assert_msg(use_features(vmaf, 0));
    mu_assert_msg(read_cpu_frames(vmaf, bpc));
    mu_assert("cpu flush failed", !vmaf_read_pictures(vmaf, NULL, NULL, 0));
    mu_assert_msg(read_scores(vmaf, scores));
    mu_assert("vmaf_close failed", !vmaf_close(vmaf));
    return NULL;
}

static char *run_zero_copy(unsigned bpc, double scores[N_FEATURES][PARITY_FRAMES])
{
    VmafSyclState *state = open_state();
    if (!state)
        return NULL;
    VmafContext *vmaf = NULL;
    char *msg = open_context(state, bpc, &vmaf);
    if (!msg)
        msg = use_features(vmaf, 1);
    for (unsigned frame = 0; !msg && frame < PARITY_FRAMES; frame++) {
        if (emulate_import(state, frame, bpc, 1)) {
            msg = "emulated import failed";
        } else if (vmaf_read_pictures_sycl(vmaf, frame)) {
            msg = "vmaf_read_pictures_sycl failed on imported chroma";
        }
    }
    if (!msg && vmaf_flush_sycl(vmaf))
        msg = "vmaf_flush_sycl failed";
    if (!msg)
        msg = read_scores(vmaf, scores);
    if (vmaf)
        (void)vmaf_close(vmaf);
    vmaf_sycl_state_free(&state);
    return msg;
}

static char *compare_scores(unsigned bpc, double cpu[N_FEATURES][PARITY_FRAMES],
                            double gpu[N_FEATURES][PARITY_FRAMES])
{
    for (unsigned f = 0; f < N_FEATURES; f++) {
        const double tol = strstr(g_features[f], "hvs") ? HVS_TOL : EXACT_TOL;
        for (unsigned frame = 0; frame < PARITY_FRAMES; frame++) {
            const double delta = fabs(cpu[f][frame] - gpu[f][frame]);
            if (delta > tol) {
                (void)fprintf(stderr, "\n%s bpc=%u frame %u: cpu=%.10f sycl=%.10f delta=%.2e\n",
                              g_features[f], bpc, frame, cpu[f][frame], gpu[f][frame], delta);
            }
            mu_assert("zero-copy chroma score differs from the CPU", delta <= tol);
        }
    }
    /* Frame 0 is checked above like every frame; make sure chroma really ran. */
    mu_assert("Cb and Cr must score differently (distinct content)",
              fabs(cpu[1][0] - cpu[2][0]) > 0.0);
    return NULL;
}

static char *check_parity(unsigned bpc)
{
    static double cpu[N_FEATURES][PARITY_FRAMES];
    static double gpu[N_FEATURES][PARITY_FRAMES];
    VmafSyclState *probe = open_state();
    if (!probe)
        return NULL;
    vmaf_sycl_state_free(&probe);
    mu_assert_msg(run_cpu(bpc, cpu));
    mu_assert_msg(run_zero_copy(bpc, gpu));
    return compare_scores(bpc, cpu, gpu);
}

static char *test_parity_8bit(void)
{
    return check_parity(8u);
}

static char *test_parity_10bit(void)
{
    return check_parity(10u);
}

/* No mark: the readers must fail loudly instead of scoring stale chroma. */
static char *test_unmarked_chroma_refused(void)
{
    VmafSyclState *state = open_state();
    if (!state)
        return NULL;
    VmafContext *vmaf = NULL;
    char *msg = open_context(state, 8u, &vmaf);
    if (!msg)
        msg = use_features(vmaf, 1);
    if (!msg && emulate_import(state, 0u, 8u, 0))
        msg = "emulated import failed";
    if (!msg && vmaf_read_pictures_sycl(vmaf, 0u) != -ENOTSUP)
        msg = "chroma readers must refuse an import that did not mark chroma";
    if (vmaf)
        (void)vmaf_close(vmaf);
    vmaf_sycl_state_free(&state);
    return msg;
}

/* ------------------------------------------------------------------ */
/* motion_sycl motion_add_uv: zero-copy chroma vs host-uploaded chroma */
/* ------------------------------------------------------------------ */

#define MOTION_FRAMES 6u
#define N_MOTION 2u

/* motion_add_uv=true is a non-default feature option, so the twin stores its
 * scores under the aliased name (ADR-1099); the default option keeps the raw one. */
static const char *const g_motion_scores[2][N_MOTION] = {
    {"VMAF_integer_feature_motion2_score", "VMAF_integer_feature_motion3_score"},
    {"integer_motion2_mau", "integer_motion3_mau"},
};

typedef struct MotionScores {
    double v[N_MOTION][MOTION_FRAMES];
    int ok[N_MOTION][MOTION_FRAMES];
} MotionScores;

static char *use_motion(VmafContext *vmaf, int add_uv)
{
    VmafFeatureDictionary *opts = NULL;
    mu_assert("dictionary set failed",
              !vmaf_feature_dictionary_set(&opts, "motion_add_uv", add_uv ? "true" : "false"));
    mu_assert("vmaf_use_feature motion_sycl failed", !vmaf_use_feature(vmaf, "motion_sycl", opts));
    return NULL;
}

static void read_motion(VmafContext *vmaf, int add_uv, MotionScores *out)
{
    for (unsigned f = 0; f < N_MOTION; f++) {
        for (unsigned frame = 0; frame < MOTION_FRAMES; frame++) {
            out->ok[f][frame] = !vmaf_feature_score_at_index(
                vmaf, g_motion_scores[add_uv ? 1 : 0][f], &out->v[f][frame], frame);
            if (!out->ok[f][frame])
                out->v[f][frame] = 0.0;
        }
    }
}

/* Host upload: pictures through vmaf_read_pictures(), chroma staged by the twin. */
static char *run_motion_host(unsigned bpc, int add_uv, MotionScores *out)
{
    VmafSyclState *state = open_state();
    if (!state)
        return NULL;
    VmafContext *vmaf = NULL;
    VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    char *msg = NULL;
    if (vmaf_init(&vmaf, cfg) || vmaf_sycl_import_state(vmaf, state))
        msg = "host leg: context setup failed";
    if (!msg)
        msg = use_motion(vmaf, add_uv);
    for (unsigned frame = 0; !msg && frame < MOTION_FRAMES; frame++) {
        VmafPicture ref;
        VmafPicture dis;
        if (fill_pic(&ref, frame, 0u, bpc) || fill_pic(&dis, frame, 1u, bpc)) {
            msg = "host leg: picture alloc failed";
        } else if (vmaf_read_pictures(vmaf, &ref, &dis, frame)) {
            msg = "host leg: vmaf_read_pictures failed";
        }
    }
    if (!msg && vmaf_read_pictures(vmaf, NULL, NULL, 0))
        msg = "host leg: flush failed";
    if (!msg)
        read_motion(vmaf, add_uv, out);
    if (vmaf)
        (void)vmaf_close(vmaf);
    vmaf_sycl_state_free(&state);
    return msg;
}

static char *run_motion_zero_copy(unsigned bpc, MotionScores *out)
{
    VmafSyclState *state = open_state();
    if (!state)
        return NULL;
    VmafContext *vmaf = NULL;
    char *msg = open_context(state, bpc, &vmaf);
    if (!msg)
        msg = use_motion(vmaf, 1);
    for (unsigned frame = 0; !msg && frame < MOTION_FRAMES; frame++) {
        if (emulate_import(state, frame, bpc, 1)) {
            msg = "emulated import failed";
        } else if (vmaf_read_pictures_sycl(vmaf, frame)) {
            msg = "vmaf_read_pictures_sycl failed on imported chroma (motion_add_uv)";
        }
    }
    if (!msg && vmaf_flush_sycl(vmaf))
        msg = "vmaf_flush_sycl failed";
    if (!msg)
        read_motion(vmaf, 1, out);
    if (vmaf)
        (void)vmaf_close(vmaf);
    vmaf_sycl_state_free(&state);
    return msg;
}

static char *compare_motion(unsigned bpc, const MotionScores *host, const MotionScores *zc,
                            const MotionScores *luma_only)
{
    unsigned defined = 0u;
    int chroma_matters = 0;
    for (unsigned f = 0; f < N_MOTION; f++) {
        for (unsigned frame = 0; frame < MOTION_FRAMES; frame++) {
            mu_assert("zero-copy and host disagree on which scores exist",
                      host->ok[f][frame] == zc->ok[f][frame]);
            if (!host->ok[f][frame])
                continue;
            defined++;
            if (fabs(host->v[f][frame] - zc->v[f][frame]) > 0.0) {
                (void)fprintf(stderr, "\n%s bpc=%u frame %u: host=%.17g zero-copy=%.17g\n",
                              g_motion_scores[1][f], bpc, frame, host->v[f][frame],
                              zc->v[f][frame]);
            }
            mu_assert("zero-copy motion_add_uv differs from host upload",
                      fabs(host->v[f][frame] - zc->v[f][frame]) <= 0.0);
            if (fabs(host->v[f][frame] - luma_only->v[f][frame]) > 0.0)
                chroma_matters = 1;
        }
    }
    (void)fprintf(stderr, "[motion bpc=%u: %u scores compared] ", bpc, defined);
    mu_assert("too few motion scores were produced", defined >= 2u * (MOTION_FRAMES - 2u));
    mu_assert("chroma must change the score (otherwise the test proves nothing)", chroma_matters);
    return NULL;
}

static char *check_motion_parity(unsigned bpc)
{
    static MotionScores host;
    static MotionScores zc;
    static MotionScores luma_only;
    VmafSyclState *probe = open_state();
    if (!probe)
        return NULL;
    vmaf_sycl_state_free(&probe);
    mu_assert_msg(run_motion_host(bpc, 1, &host));
    mu_assert_msg(run_motion_host(bpc, 0, &luma_only));
    mu_assert_msg(run_motion_zero_copy(bpc, &zc));
    return compare_motion(bpc, &host, &zc, &luma_only);
}

static char *test_motion_add_uv_8bit(void)
{
    return check_motion_parity(8u);
}

static char *test_motion_add_uv_10bit(void)
{
    return check_motion_parity(10u);
}

/* No mark: motion_add_uv must refuse instead of differencing stale chroma. */
static char *test_motion_add_uv_unmarked_refused(void)
{
    VmafSyclState *state = open_state();
    if (!state)
        return NULL;
    VmafContext *vmaf = NULL;
    char *msg = open_context(state, 8u, &vmaf);
    if (!msg)
        msg = use_motion(vmaf, 1);
    if (!msg && emulate_import(state, 0u, 8u, 0))
        msg = "emulated import failed";
    if (!msg && vmaf_read_pictures_sycl(vmaf, 0u) != -ENOTSUP)
        msg = "motion_add_uv must refuse an import that did not mark chroma";
    if (vmaf)
        (void)vmaf_close(vmaf);
    vmaf_sycl_state_free(&state);
    return msg;
}

/* ------------------------------------------------------------------ */
/* Luma-only float twins: zero-copy vs host upload vs CPU (ADR-1598)   */
/* ------------------------------------------------------------------ */

#define LUMA_FRAMES 5u
#define LUMA_MAX_SCORES 4u

typedef struct LumaFeature {
    const char *twin;
    const char *cpu;
    const char *scores[LUMA_MAX_SCORES];
    unsigned n_scores;
    const char *opt_key; /* optional extractor option, set on both legs */
    const char *opt_val;
} LumaFeature;

static const LumaFeature g_luma_features[] = {
    {"float_psnr_sycl", "float_psnr", {"float_psnr"}, 1u, NULL, NULL},
    {"float_motion_sycl",
     "float_motion",
     {"VMAF_feature_motion2_score", "VMAF_feature_motion3_score"},
     2u,
     NULL,
     NULL},
    {"float_vif_sycl",
     "float_vif",
     {"VMAF_feature_vif_scale0_score", "VMAF_feature_vif_scale1_score",
      "VMAF_feature_vif_scale2_score", "VMAF_feature_vif_scale3_score"},
     4u,
     NULL,
     NULL},
    {"float_adm_sycl",
     "float_adm",
     {"VMAF_feature_adm2_score", "VMAF_feature_adm_scale0_score", "VMAF_feature_adm_scale1_score",
      "VMAF_feature_adm_scale2_score"},
     4u,
     NULL,
     NULL},
    {"float_ssim_sycl", "float_ssim", {"float_ssim"}, 1u, NULL, NULL},
    /* scale=2 runs the float_ssim twin device decimation (33x18 samples from 67x37). */
    {"float_ssim_sycl", "float_ssim", {"float_ssim"}, 1u, "scale", "2"},
    {"integer_ssim_sycl", "ssim", {"ssim"}, 1u, NULL, NULL},
};
#define N_LUMA_FEATURES ((unsigned)(sizeof(g_luma_features) / sizeof(g_luma_features[0])))

typedef struct LumaScores {
    double v[LUMA_MAX_SCORES][LUMA_FRAMES];
    int ok[LUMA_MAX_SCORES][LUMA_FRAMES];
} LumaScores;

static void read_luma_scores(VmafContext *vmaf, const LumaFeature *lf, LumaScores *out)
{
    for (unsigned f = 0; f < lf->n_scores; f++) {
        for (unsigned frame = 0; frame < LUMA_FRAMES; frame++) {
            out->ok[f][frame] =
                !vmaf_feature_score_at_index(vmaf, lf->scores[f], &out->v[f][frame], frame);
            if (!out->ok[f][frame])
                out->v[f][frame] = 0.0;
        }
    }
}

static int use_luma_feature(VmafContext *vmaf, const char *name, const LumaFeature *lf)
{
    VmafFeatureDictionary *opts = NULL;
    if (lf->opt_key && vmaf_feature_dictionary_set(&opts, lf->opt_key, lf->opt_val))
        return -ENOMEM;
    return vmaf_use_feature(vmaf, name, opts);
}

/* CPU extractor (sycl == 0) or the SYCL twin on host pictures (sycl == 1). */
static char *run_luma_host(const LumaFeature *lf, unsigned bpc, int sycl, LumaScores *out)
{
    VmafSyclState *state = NULL;
    if (sycl) {
        state = open_state();
        if (!state)
            return NULL;
    }
    VmafContext *vmaf = NULL;
    VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    char *msg = NULL;
    if (vmaf_init(&vmaf, cfg) || (state && vmaf_sycl_import_state(vmaf, state)) ||
        use_luma_feature(vmaf, sycl ? lf->twin : lf->cpu, lf))
        msg = "host leg: context setup failed";
    for (unsigned frame = 0; !msg && frame < LUMA_FRAMES; frame++) {
        VmafPicture ref;
        VmafPicture dis;
        if (fill_pic(&ref, frame, 0u, bpc) || fill_pic(&dis, frame, 1u, bpc)) {
            msg = "host leg: picture alloc failed";
        } else if (vmaf_read_pictures(vmaf, &ref, &dis, frame)) {
            msg = "host leg: vmaf_read_pictures failed";
        }
    }
    if (!msg && vmaf_read_pictures(vmaf, NULL, NULL, 0))
        msg = "host leg: flush failed";
    if (!msg)
        read_luma_scores(vmaf, lf, out);
    if (vmaf)
        (void)vmaf_close(vmaf);
    if (state)
        vmaf_sycl_state_free(&state);
    return msg;
}

/* Luma through the upload slots only, as the VA import writes it; no chroma
 * mark, so a luma-only twin must not need one. */
static char *run_luma_zero_copy(const LumaFeature *lf, unsigned bpc, LumaScores *out)
{
    VmafSyclState *state = open_state();
    if (!state)
        return NULL;
    VmafContext *vmaf = NULL;
    char *msg = open_context(state, bpc, &vmaf);
    if (!msg && use_luma_feature(vmaf, lf->twin, lf))
        msg = "vmaf_use_feature failed";
    for (unsigned frame = 0; !msg && frame < LUMA_FRAMES; frame++) {
        if (emulate_import(state, frame, bpc, 0)) {
            msg = "emulated import failed";
        } else if (vmaf_read_pictures_sycl(vmaf, frame)) {
            msg = "a luma-only SYCL twin must score on zero-copy input";
        }
    }
    if (!msg && vmaf_flush_sycl(vmaf))
        msg = "vmaf_flush_sycl failed";
    if (!msg)
        read_luma_scores(vmaf, lf, out);
    if (vmaf)
        (void)vmaf_close(vmaf);
    vmaf_sycl_state_free(&state);
    return msg;
}

static int same_bits(double a, double b)
{
    return memcmp(&a, &b, sizeof(a)) == 0;
}

/* Every score the two runs hold must be present in both and identical bits. */
static char *compare_luma(const LumaFeature *lf, unsigned bpc, const char *what,
                          const LumaScores *want, const LumaScores *got, unsigned *defined)
{
    for (unsigned f = 0; f < lf->n_scores; f++) {
        for (unsigned frame = 0; frame < LUMA_FRAMES; frame++) {
            mu_assert("the two runs disagree on which scores exist",
                      want->ok[f][frame] == got->ok[f][frame]);
            if (!want->ok[f][frame])
                continue;
            *defined += 1u;
            if (!same_bits(want->v[f][frame], got->v[f][frame])) {
                (void)fprintf(stderr, "\n%s %s bpc=%u frame %u: want=%.17g got=%.17g\n", what,
                              lf->scores[f], bpc, frame, want->v[f][frame], got->v[f][frame]);
            }
            mu_assert("scores differ between runs", same_bits(want->v[f][frame], got->v[f][frame]));
        }
    }
    return NULL;
}

static char *check_luma_twin(const LumaFeature *lf, unsigned bpc)
{
    static LumaScores cpu;
    static LumaScores host;
    static LumaScores zc;
    VmafSyclState *probe = open_state();
    if (!probe)
        return NULL;
    vmaf_sycl_state_free(&probe);
    mu_assert_msg(run_luma_host(lf, bpc, 0, &cpu));
    mu_assert_msg(run_luma_host(lf, bpc, 1, &host));
    mu_assert_msg(run_luma_zero_copy(lf, bpc, &zc));
    unsigned defined = 0u;
    mu_assert_msg(compare_luma(lf, bpc, "host upload vs CPU", &cpu, &host, &defined));
    mu_assert_msg(compare_luma(lf, bpc, "zero-copy vs host upload", &host, &zc, &defined));
    (void)fprintf(stderr, "[%s bpc=%u: %u scores compared] ", lf->twin, bpc, defined);
    mu_assert("too few scores were produced", defined >= 2u * lf->n_scores * (LUMA_FRAMES - 2u));
    return NULL;
}

static char *test_luma_twins_8bit(void)
{
    for (unsigned i = 0; i < N_LUMA_FEATURES; i++)
        mu_assert_msg(check_luma_twin(&g_luma_features[i], 8u));
    return NULL;
}

static char *test_luma_twins_10bit(void)
{
    for (unsigned i = 0; i < N_LUMA_FEATURES; i++)
        mu_assert_msg(check_luma_twin(&g_luma_features[i], 10u));
    return NULL;
}

static char *run_chroma_tests(void)
{
    mu_run_test(test_eager_chroma_allocation);
    mu_run_test(test_chroma_currency_contract);
    mu_run_test(test_parity_8bit);
    mu_run_test(test_parity_10bit);
    mu_run_test(test_unmarked_chroma_refused);
    return NULL;
}

static char *run_motion_tests(void)
{
    mu_run_test(test_motion_add_uv_8bit);
    mu_run_test(test_motion_add_uv_10bit);
    mu_run_test(test_motion_add_uv_unmarked_refused);
    return NULL;
}

static char *run_luma_tests(void)
{
    mu_run_test(test_luma_twins_8bit);
    mu_run_test(test_luma_twins_10bit);
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(run_chroma_tests);
    mu_run_test(run_motion_tests);
    mu_run_test(run_luma_tests);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
