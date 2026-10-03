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
 *  - without a mark the chroma readers fail with -ENOTSUP.
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

static char *check_currency(VmafSyclState *state)
{
    mu_assert("chroma is not current before any import", !vmaf_sycl_shared_chroma_current(state));
    /* Both slots start at 0; one advance separates upload from compute. */
    vmaf_sycl_advance_frame(state);
    mu_assert("an advance without any mark leaves chroma stale",
              !vmaf_sycl_shared_chroma_current(state));

    void *const up_ref = vmaf_sycl_get_shared_plane_upload(state, 1, 1u);
    void *const up_dis = vmaf_sycl_get_shared_plane_upload(state, 0, 2u);
    mu_assert("upload slot differs from the compute slot",
              up_ref != vmaf_sycl_get_shared_plane(state, 1, 1u) &&
                  up_dis != vmaf_sycl_get_shared_plane(state, 0, 2u));
    mu_assert("plane 0 upload slot is the luma upload slot",
              vmaf_sycl_get_shared_plane_upload(state, 1, 0u) ==
                  vmaf_sycl_get_shared_ref_upload(state));

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

char *run_tests(void)
{
    mu_run_test(test_eager_chroma_allocation);
    mu_run_test(test_chroma_currency_contract);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
