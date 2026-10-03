/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * Zero-copy guards for vmaf_read_pictures_sycl (ADR-1595).
 *
 * Zero-copy input (a VA surface imported straight into device memory) gives
 * the extractors no host pictures. A CPU extractor cannot run there and must
 * be refused with -ENOTSUP before any state changes, never skipped silently.
 * The test writes the shared upload slots directly (what the VA import does),
 * registers a CPU extractor next to a SYCL twin, and checks that the call is
 * rejected both times without a crash, then that a context holding only the
 * luma-only SYCL twin still scores. A guard table then runs every SYCL extractor on
 * zero-copy input: the ones that work score, the ones that need host pictures must return
 * -ENOTSUP and not crash (ADR-1595).
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

#include "libvmaf/libvmaf.h"
#include "libvmaf/libvmaf_sycl.h"
#include "feature/feature_extractor.h"
#include "sycl/common.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy also proposes the `nullptr` keyword, but this is a C
 * translation unit whose sources spell the null pointer constant `NULL` and
 * MSVC's documented /std:clatest C23 feature set does not include `nullptr`
 * while the required Windows build compiles this TU with cl.exe. ADR-1138. */

#define FRAME_W 352u
#define FRAME_H 192u
#define N_FRAMES 3u
#define VIF_SCORE "VMAF_integer_feature_vif_scale0_score"

static unsigned luma_sample(unsigned row, unsigned col, unsigned frame, unsigned salt, unsigned bpc)
{
    const unsigned mask = (1u << bpc) - 1u;
    return ((row * 7u) ^ (col * 5u) ^ (frame * 31u + salt * 17u)) & mask;
}

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

/* One frame of synthetic luma in the sample width the bit depth needs. */
static void fill_luma(uint8_t *buf, unsigned frame, unsigned salt, unsigned bpc)
{
    const size_t bytes_per_px = bpc > 8u ? 2u : 1u;
    for (unsigned row = 0; row < FRAME_H; row++) {
        for (unsigned col = 0; col < FRAME_W; col++) {
            const unsigned v = luma_sample(row, col, frame, salt, bpc);
            const size_t at = ((size_t)row * FRAME_W + col) * bytes_per_px;
            buf[at] = (uint8_t)(v & 0xFFu);
            if (bytes_per_px == 2u) {
                buf[at + 1u] = (uint8_t)(v >> 8);
            }
        }
    }
}

/* Fill the upload slots with one frame of luma, as the VA import does. */
static int write_luma_bpc(VmafSyclState *state, unsigned frame, unsigned bpc)
{
    const unsigned pitch = FRAME_W * (bpc > 8u ? 2u : 1u);
    uint8_t *buf = malloc((size_t)pitch * FRAME_H);
    if (!buf) {
        return -ENOMEM;
    }
    int err = 0;
    /* vmaf_sycl_upload_plane() returns after its copy, so the buffer can be
     * refilled for the other plane and freed right away. */
    for (unsigned is_ref = 0; is_ref < 2u && !err; is_ref++) {
        fill_luma(buf, frame, is_ref, bpc);
        err = vmaf_sycl_upload_plane(state, buf, pitch, (int)is_ref, FRAME_W, FRAME_H, bpc);
    }
    free(buf);
    return err;
}

static int write_luma(VmafSyclState *state, unsigned frame)
{
    return write_luma_bpc(state, frame, 8u);
}

static char *open_context(VmafSyclState *state, VmafContext **vmaf, const char *const *names,
                          unsigned n_names)
{
    VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    mu_assert("vmaf_init failed", !vmaf_init(vmaf, cfg));
    mu_assert("vmaf_sycl_import_state failed", !vmaf_sycl_import_state(*vmaf, state));
    mu_assert("vmaf_sycl_init_frame_buffers failed",
              !vmaf_sycl_init_frame_buffers(*vmaf, FRAME_W, FRAME_H, 8u));
    for (unsigned i = 0; i < n_names; i++) {
        mu_assert("vmaf_use_feature failed", !vmaf_use_feature(*vmaf, names[i], NULL));
    }
    return NULL;
}

static char *test_cpu_extractor_rejected_before_state_change(void)
{
    VmafSyclState *state = open_state();
    if (!state) {
        return NULL;
    }
    static const char *const mixed[] = {"psnr", "vif_sycl"};
    VmafContext *vmaf = NULL;
    char *msg = open_context(state, &vmaf, mixed, 2u);
    if (!msg && write_luma(state, 0u)) {
        msg = "luma upload failed";
    }
    if (!msg && vmaf_read_pictures_sycl(vmaf, 0u) != -ENOTSUP) {
        msg = "a registered CPU extractor must be rejected with -ENOTSUP";
    }
    /* No half-advanced frame: the same call is rejected again, no crash. */
    if (!msg && vmaf_read_pictures_sycl(vmaf, 0u) != -ENOTSUP) {
        msg = "the rejection must leave the context unchanged";
    }
    if (vmaf) {
        (void)vmaf_close(vmaf);
    }
    vmaf_sycl_state_free(&state);
    return msg;
}

static char *test_luma_only_twin_scores(void)
{
    VmafSyclState *state = open_state();
    if (!state) {
        return NULL;
    }
    static const char *const twin_only[] = {"vif_sycl"};
    VmafContext *vmaf = NULL;
    char *msg = open_context(state, &vmaf, twin_only, 1u);
    for (unsigned frame = 0; !msg && frame < N_FRAMES; frame++) {
        if (write_luma(state, frame)) {
            msg = "luma upload failed";
        } else if (vmaf_read_pictures_sycl(vmaf, frame)) {
            msg = "a SYCL twin on zero-copy input must score";
        }
    }
    if (!msg && vmaf_flush_sycl(vmaf)) {
        msg = "vmaf_flush_sycl failed";
    }
    for (unsigned frame = 0; !msg && frame < N_FRAMES; frame++) {
        double score = NAN;
        if (vmaf_feature_score_at_index(vmaf, VIF_SCORE, &score, frame)) {
            msg = "vif score missing";
        } else if (!isfinite(score)) {
            msg = "vif score is not finite";
        }
    }
    if (vmaf) {
        (void)vmaf_close(vmaf);
    }
    vmaf_sycl_state_free(&state);
    return msg;
}

/* One row per SYCL extractor configuration. expect is 0 (scores on zero-copy input) or
 * -ENOTSUP (needs host pictures). */
typedef struct {
    const char *name;
    const char *opt_key;
    const char *opt_val;
    unsigned bpc;
    int expect;
    const char *score; /* checked at frame 1 when expect == 0 */
    int chroma;        /* write Cb / Cr into the upload slots and mark them imported */
} GuardRow;

static const GuardRow guard_rows[] = {
    {"adm_sycl", NULL, NULL, 8u, 0, "VMAF_integer_feature_adm2_score", 0},
    {"cambi_sycl", NULL, NULL, 8u, 0, "Cambi_feature_cambi_score", 0},
    {"float_moment_sycl", NULL, NULL, 8u, 0, "float_moment_ref1st", 0},
    {"motion_sycl", NULL, NULL, 8u, 0, "VMAF_integer_feature_motion2_score", 0},
    {"motion_v2_sycl", NULL, NULL, 8u, 0, "VMAF_integer_feature_motion2_v2_score", 0},
    {"vif_sycl", NULL, NULL, 8u, 0, VIF_SCORE, 0},
    {"psnr_sycl", "enable_chroma", "false", 8u, 0, "psnr_y", 0},
    {"psnr_hvs_sycl", "enable_chroma", "false", 8u, 0, "psnr_hvs_y", 0},
    {"float_ms_ssim_sycl", NULL, NULL, 8u, -ENOTSUP, NULL, 0},
    {"float_psnr_sycl", NULL, NULL, 8u, 0, "float_psnr", 0},
    {"float_psnr_sycl", NULL, NULL, 10u, 0, "float_psnr", 0},
    {"float_adm_sycl", NULL, NULL, 8u, 0, "VMAF_feature_adm2_score", 0},
    {"float_vif_sycl", NULL, NULL, 8u, 0, "VMAF_feature_vif_scale0_score", 0},
    {"float_motion_sycl", NULL, NULL, 8u, 0, "VMAF_feature_motion2_score", 0},
    {"integer_ssim_sycl", NULL, NULL, 8u, 0, "ssim", 0},
    {"float_ssim_sycl", NULL, NULL, 8u, 0, "float_ssim", 0},
    {"ciede_sycl", NULL, NULL, 8u, -ENOTSUP, NULL, 0},
    {"ciede_sycl", NULL, NULL, 8u, 0, "ciede2000", 1},
    {"ssimulacra2_sycl", NULL, NULL, 8u, -ENOTSUP, NULL, 0},
    {"ssimulacra2_sycl", NULL, NULL, 8u, 0, "ssimulacra2", 1},
    {"speed_chroma_sycl", NULL, NULL, 8u, -ENOTSUP, NULL, 0},
    {"speed_temporal_sycl", NULL, NULL, 8u, 0, "Speed_temporal_feature_speed_temporal_score", 0},
    {"psnr_sycl", NULL, NULL, 8u, -ENOTSUP, NULL, 0},
    {"psnr_hvs_sycl", NULL, NULL, 8u, -ENOTSUP, NULL, 0},
    {"motion_sycl", "motion_add_uv", "true", 8u, -ENOTSUP, NULL, 0},
};
#define N_GUARD_ROWS ((unsigned)(sizeof(guard_rows) / sizeof(guard_rows[0])))

#ifndef EXPECTED_SYCL_EXTRACTORS
#error "meson passes the number of SYCL extractors found under core/src/feature/sycl"
#endif

/* Every registered SYCL extractor must have a row (the registry is not iterable, so meson
 * counts the `.name = "..._sycl"` registrations at configure time). */
static char *test_table_covers_every_sycl_extractor(void)
{
    unsigned distinct = 0;
    for (unsigned i = 0; i < N_GUARD_ROWS; i++) {
        const VmafFeatureExtractor *fex = vmaf_get_feature_extractor_by_name(guard_rows[i].name);
        mu_assert("guard row names an extractor that is not registered", fex != NULL);
        mu_assert("guard row names a non-SYCL extractor", fex->flags & VMAF_FEATURE_EXTRACTOR_SYCL);
        int seen = 0;
        for (unsigned j = 0; j < i; j++) {
            seen |= guard_rows[j].name == guard_rows[i].name ||
                    !strcmp(guard_rows[j].name, guard_rows[i].name);
        }
        distinct += seen ? 0u : 1u;
    }
    mu_assert("a SYCL extractor is missing from the zero-copy guard table",
              distinct == (unsigned)EXPECTED_SYCL_EXTRACTORS);
    return NULL;
}

static int guard_use_feature(VmafContext *vmaf, const GuardRow *row)
{
    VmafFeatureDictionary *opts = NULL;
    if (row->opt_key && vmaf_feature_dictionary_set(&opts, row->opt_key, row->opt_val)) {
        return -ENOMEM;
    }
    /* vmaf_use_feature consumes opts on success and failure of the lookup. */
    return vmaf_use_feature(vmaf, row->name, opts);
}

/* Cb / Cr of one frame into the upload slots, then the mark, as the VA import does. */
static int write_chroma_marked(VmafSyclState *state, unsigned frame, unsigned bpc)
{
    const unsigned cw = (FRAME_W + 1u) / 2u;
    const unsigned ch = (FRAME_H + 1u) / 2u;
    const size_t bytes = (size_t)cw * ch * (bpc > 8u ? 2u : 1u);
    uint8_t *buf = malloc(bytes);
    if (!buf) {
        return -ENOMEM;
    }
    int err = 0;
    for (unsigned is_ref = 0; is_ref < 2u && !err; is_ref++) {
        for (unsigned plane = 1u; plane < 3u && !err; plane++) {
            for (size_t i = 0; i < bytes; i++) {
                buf[i] = (uint8_t)((i * (3u + plane) + frame * 11u + is_ref * 7u) & 0xFFu);
            }
            err = vmaf_sycl_memcpy_h2d(
                state, vmaf_sycl_get_shared_plane_upload(state, (int)is_ref, plane), buf, bytes);
        }
    }
    free(buf);
    if (!err) {
        vmaf_sycl_shared_chroma_mark_imported(state);
    }
    return err;
}

/* Drive three zero-copy frames plus a flush; return the first non-zero status. */
static int guard_run_frames(VmafContext *vmaf, VmafSyclState *state, const GuardRow *row)
{
    int err = 0;
    for (unsigned frame = 0; !err && frame < N_FRAMES; frame++) {
        err = write_luma_bpc(state, frame, row->bpc);
        if (!err && row->chroma) {
            err = write_chroma_marked(state, frame, row->bpc);
        }
        if (!err) {
            err = vmaf_read_pictures_sycl(vmaf, frame);
        }
    }
    return err ? err : vmaf_flush_sycl(vmaf);
}

static const char *guard_check_row(const GuardRow *row)
{
    VmafSyclState *state = NULL;
    VmafSyclConfiguration scfg = {.device_index = -1};
    if (vmaf_sycl_state_init(&state, scfg) != 0 || !state) {
        return "no device";
    }
    VmafContext *vmaf = NULL;
    VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    const char *msg = NULL;
    if (vmaf_init(&vmaf, cfg) || vmaf_sycl_import_state(vmaf, state) ||
        vmaf_sycl_init_frame_buffers(vmaf, FRAME_W, FRAME_H, row->bpc) ||
        guard_use_feature(vmaf, row)) {
        msg = "context setup failed";
    }
    const int err = msg ? 0 : guard_run_frames(vmaf, state, row);
    if (!msg && err != row->expect) {
        msg = row->expect ? "expected -ENOTSUP" : "expected the extractor to score";
    }
    for (unsigned frame = 1; !msg && !row->expect && row->score && frame < 2u; frame++) {
        double score = NAN;
        if (vmaf_feature_score_at_index(vmaf, row->score, &score, frame)) {
            msg = "score missing";
        } else if (!isfinite(score)) {
            msg = "score is not finite";
        }
    }
    if (vmaf) {
        (void)vmaf_close(vmaf);
    }
    vmaf_sycl_state_free(&state);
    return msg;
}

static char *test_guard_table(void)
{
    VmafSyclState *probe = open_state();
    if (!probe) {
        return NULL;
    }
    vmaf_sycl_state_free(&probe);
    unsigned failed = 0;
    for (unsigned i = 0; i < N_GUARD_ROWS; i++) {
        const GuardRow *row = &guard_rows[i];
        const char *msg = guard_check_row(row);
        (void)fprintf(stderr, "  guard %-20s bpc=%-2u%s %s%s\n", row->name, row->bpc,
                      row->chroma ? " chroma" : "", msg ? "FAIL: " : "ok", msg ? msg : "");
        failed += msg ? 1u : 0u;
    }
    mu_assert("a zero-copy guard row failed (see the log above)", failed == 0u);
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_cpu_extractor_rejected_before_state_change);
    mu_run_test(test_luma_only_twin_scores);
    mu_run_test(test_table_covers_every_sycl_extractor);
    mu_run_test(test_guard_table);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
