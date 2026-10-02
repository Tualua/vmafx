/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * speed_chroma CPU vs. HIP parity test (ADR-1004 round 5; bound from
 * ADR-1430).
 *
 * Asserts that the three scores of the CPU extractor `speed_chroma` and of
 * the HIP twin `speed_chroma_hip` agree at every frame to one part in a
 * million. The twin runs speed.c's arithmetic and rounds log2 correctly
 * (`speed_hd_log2_rn()`, ADR-1384); speed.c calls the C library's `log2f`.
 * The fixture, the CPU run, the comparison and the reason it is a bound and
 * not an equality are in speed_chroma_twin_parity.h, which the CUDA test
 * wraps too.
 *
 * The test compared `speed_chroma_uv` of frame 0 within 1e-4 on a 768x432
 * fixture before: that fixture has 8 blocks for a 25x25 covariance, which is
 * singular on every frame, so the scoring path with its log2 never ran.
 *
 * Skip behaviour, both exit 77 (meson's "skipped" status):
 *   1. `vmaf_hip_state_init()` fails (no HIP/ROCm runtime or no device
 *      visible): emits `[skip: no HIP device]`.
 *   2. `vmaf_use_feature()` or the first submit returns `-ENOSYS` (library
 *      built with `enable_hipcc=false`, the scaffold posture of the
 *      `#ifndef HAVE_HIPCC` guard in `speed_chroma_hip.c`): emits
 *      `[skip: HIP scaffold ENOSYS]`.
 */

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test.h"

#include "hip_parity_skip.h"
#include "speed_chroma_twin_parity.h"

#include "libvmaf/libvmaf.h"
#include "libvmaf/libvmaf_hip.h"
#include "libvmaf/picture.h"

/* NOLINTBEGIN(modernize-use-nullptr): this is a
 * C23 translation unit, but the required MSVC C lane does not provide the C
 * nullptr spelling clang-tidy proposes. Keep the portable C API form under
 * ADR-1138. */

/* Opens a HIP state and a context with `speed_chroma_hip` registered. Leaves
 * `*out_vmaf` NULL and sets `*skipped` when there is no device or the twin is
 * a scaffold. */
static char *setup_hip_speed_chroma_context(VmafContext **out_vmaf, VmafHipState **out_hip_state,
                                            int *skipped)
{
    *out_vmaf = NULL;
    *out_hip_state = NULL;

    VmafHipConfiguration hip_cfg = {.device_index = -1};
    int err = vmaf_hip_state_init(out_hip_state, hip_cfg);
    if (err != 0 || *out_hip_state == NULL) {
        (void)fprintf(stderr, "[skip: no HIP device] ");
        *skipped = 1;
        return NULL;
    }

    VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    VmafContext *vmaf = NULL;
    err = vmaf_init(&vmaf, cfg);
    mu_assert("HIP: vmaf_init failed", !err);

    err = vmaf_hip_import_state(vmaf, *out_hip_state);
    mu_assert("HIP: vmaf_hip_import_state failed", !err);

    err = vmaf_use_feature(vmaf, "speed_chroma_hip", NULL);
    if (err == -ENOSYS)
        return hip_parity_skip(vmaf, out_hip_state, skipped, "");
    mu_assert("HIP: vmaf_use_feature(speed_chroma_hip) failed", !err);

    *out_vmaf = vmaf;
    return NULL;
}

static char *run_hip(SpeedChromaTwinScores *out_scores, int *skipped)
{
    speed_chroma_twin_clear(out_scores);

    VmafContext *vmaf = NULL;
    VmafHipState *hip_state = NULL;
    mu_assert_msg(setup_hip_speed_chroma_context(&vmaf, &hip_state, skipped));
    if (!vmaf)
        return NULL;

    int err = speed_chroma_twin_feed(vmaf);
    if (err == -ENOSYS)
        return hip_parity_skip(vmaf, &hip_state, skipped, " on submit");
    mu_assert("HIP: feeding the frames failed", !err);
    mu_assert_msg(speed_chroma_twin_read(vmaf, out_scores));

    err = vmaf_close(vmaf);
    mu_assert("HIP: vmaf_close failed", !err);

    vmaf_hip_state_free(&hip_state);
    return NULL;
}

static char *test_speed_chroma_cpu_hip_parity(void)
{
    SpeedChromaTwinScores cpu;
    SpeedChromaTwinScores hip;
    int skipped = 0;

    char *msg = speed_chroma_twin_run_cpu(&cpu);
    if (msg)
        return msg;

    msg = run_hip(&hip, &skipped);
    if (msg)
        return msg;

    if (skipped) {
        mu_skipped = 1; /* exit 77, not a pass */
        return NULL;
    }

    mu_assert("speed_chroma_hip is further from the CPU extractor than its log2f explains",
              speed_chroma_twin_outside_bound(&cpu, &hip, "hip") == 0u);
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_speed_chroma_cpu_hip_parity);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
