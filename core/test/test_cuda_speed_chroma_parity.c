/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * speed_chroma CPU vs. CUDA parity test (ADR-0965; equality since ADR-1477).
 *
 * Asserts that the three scores of the CPU extractor `speed_chroma` and of
 * the CUDA twin `speed_chroma_cuda` are equal at every frame. The fixture,
 * the CPU run, the comparison and the reason it is an equality (the twin
 * evaluates Netflix's fp64 statements, the logarithms on the host) are in
 * speed_chroma_twin_parity.h, which the HIP test wraps too. The test allowed
 * 1e-4 on one score of one frame, then one part in a million, while the
 * fork's speed.c called `log2f`.
 *
 * Skip behaviour: if `vmaf_cuda_state_init()` fails (no CUDA driver or no
 * device visible) the test emits `[skip: no CUDA device]` and exits 77, meson's
 * "skipped" status.
 * Mirrors the pattern from test_cuda_motion3_parity.c.
 */

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test.h"

#include "speed_chroma_twin_parity.h"

#include "libvmaf/libvmaf.h"
#include "libvmaf/libvmaf_cuda.h"
#include "libvmaf/picture.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy also proposes the `nullptr` keyword, but MSVC's
 * documented /std:clatest C23 feature set does not include `nullptr` while the
 * required Windows build compiles this TU with cl.exe, and this test mirrors
 * the C spelling of the surface it exercises. ADR-1138. */

static char *setup_cuda_speed_chroma_context(VmafContext **out_vmaf, VmafCudaState **out_cu_state)
{
    *out_vmaf = NULL;
    *out_cu_state = NULL;

    VmafCudaState *cu_state = NULL;
    VmafCudaConfiguration cuda_cfg = {0};
    int err = vmaf_cuda_state_init(&cu_state, cuda_cfg);
    if (err != 0 || cu_state == NULL) {
        (void)fprintf(stderr, "[skip: no CUDA device] ");
        mu_skipped = 1; /* exit 77, not a pass */
        return NULL;
    }

    VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    VmafContext *vmaf = NULL;
    err = vmaf_init(&vmaf, cfg);
    mu_assert("CUDA: vmaf_init failed", !err);

    err = vmaf_cuda_import_state(vmaf, cu_state);
    mu_assert("CUDA: vmaf_cuda_import_state failed", !err);

    err = vmaf_use_feature(vmaf, "speed_chroma_cuda", NULL);
    mu_assert("CUDA: vmaf_use_feature(speed_chroma_cuda) failed", !err);

    *out_vmaf = vmaf;
    *out_cu_state = cu_state;
    return NULL;
}

static char *run_cuda(SpeedChromaTwinScores *out_scores)
{
    speed_chroma_twin_clear(out_scores);

    VmafContext *vmaf = NULL;
    VmafCudaState *cu_state = NULL;
    mu_assert_msg(setup_cuda_speed_chroma_context(&vmaf, &cu_state));
    if (!vmaf)
        return NULL;

    int err = speed_chroma_twin_feed(vmaf);
    mu_assert("CUDA: feeding the frames failed", !err);
    mu_assert_msg(speed_chroma_twin_read(vmaf, out_scores));

    err = vmaf_close(vmaf);
    mu_assert("CUDA: vmaf_close failed", !err);

    err = vmaf_cuda_state_free(cu_state);
    mu_assert("CUDA: vmaf_cuda_state_free failed", !err);
    return NULL;
}

static char *test_speed_chroma_cpu_cuda_parity(void)
{
    SpeedChromaTwinScores cpu;
    SpeedChromaTwinScores cuda;

    char *msg = speed_chroma_twin_run_cpu(&cpu);
    if (msg)
        return msg;

    msg = run_cuda(&cuda);
    if (msg)
        return msg;

    if (mu_skipped)
        return NULL;

    mu_assert("speed_chroma_cuda does not return the CPU extractor's scores",
              speed_chroma_twin_mismatches(&cpu, &cuda, "cuda") == 0u);
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_speed_chroma_cpu_cuda_parity);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
