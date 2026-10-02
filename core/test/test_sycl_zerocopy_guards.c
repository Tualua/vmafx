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
 * luma-only SYCL twin still scores.
 *
 * Skip behaviour: without a SYCL device the test prints
 * "[skip: no SYCL device]" and passes, like test_sycl_shared_planes.c.
 */

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "test.h"

#include "libvmaf/libvmaf.h"
#include "libvmaf/libvmaf_sycl.h"
#include "sycl/common.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy also proposes the `nullptr` keyword, but this is a C
 * translation unit whose sources spell the null pointer constant `NULL` and
 * MSVC's documented /std:clatest C23 feature set does not include `nullptr`
 * while the required Windows build compiles this TU with cl.exe. ADR-1138. */

#define FRAME_W 128u
#define FRAME_H 72u
#define N_FRAMES 3u
#define VIF_SCORE "VMAF_integer_feature_vif_scale0_score"

static uint8_t luma_sample(unsigned row, unsigned col, unsigned frame, unsigned salt)
{
    return (uint8_t)(((row * 7u) ^ (col * 5u) ^ (frame * 31u + salt * 17u)) & 0xFFu);
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

/* Fill the upload slots with one frame of luma, as the VA import does. */
static int write_luma(VmafSyclState *state, unsigned frame)
{
    uint8_t *buf = malloc((size_t)FRAME_W * FRAME_H);
    if (!buf) {
        return -ENOMEM;
    }
    int err = 0;
    for (unsigned is_ref = 0; is_ref < 2u && !err; is_ref++) {
        for (unsigned row = 0; row < FRAME_H; row++) {
            for (unsigned col = 0; col < FRAME_W; col++) {
                buf[(size_t)row * FRAME_W + col] = luma_sample(row, col, frame, is_ref);
            }
        }
        err = vmaf_sycl_upload_plane(state, buf, FRAME_W, (int)is_ref, FRAME_W, FRAME_H, 8u);
    }
    free(buf);
    if (err) {
        return err;
    }
    return vmaf_sycl_wait_copy_queue(state);
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

char *run_tests(void)
{
    mu_run_test(test_cpu_extractor_rejected_before_state_change);
    mu_run_test(test_luma_only_twin_scores);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
