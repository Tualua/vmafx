/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * `float_ssim_cuda` adds its per-window terms in the CPU's raster order
 * (T-GPU-FLOAT-SSIM-FRAME-SUM-ORDER-2026-10-02).
 *
 * iqa/ssim_tools.c::iqa_ssim() adds every window's l * c * s into one double,
 * left to right and top to bottom, and returns the mean as a float. The twin
 * computed the same terms and added them per warp, per block and then on the
 * host. A sum of doubles is its order, and on the frame pair of
 * float_ssim_order_frame.h the two orders end on different sides of a float
 * rounding boundary: the CPU returns 0xb4e2b622 and the per-block sum returned
 * 0xb4e2b621.
 *
 * The test scores that pair on the CPU extractor and on the twin, with and
 * without `enable_lcs` (the two pass-2 kernels), and requires
 *   - the CPU's `float_ssim` to have the bits the header records, so the
 *     fixture is still the frame it was built to be on this build, and
 *   - every output of the twin to have the CPU's bits.
 * It fails on a twin that reduces the terms on the device.
 *
 * Skip behaviour: without a CUDA device the test reports the skip and the
 * run exits 77.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test.h"

#include "float_ssim_order_frame.h"

#include "libvmaf/feature.h"
#include "libvmaf/libvmaf.h"
#include "libvmaf/libvmaf_cuda.h"
#include "libvmaf/picture.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy also proposes the `nullptr` keyword, but MSVC's
 * documented /std:clatest C23 feature set does not include `nullptr` while the
 * required Windows build compiles this TU with cl.exe, and this file mirrors
 * the C spelling of the surface it exercises. ADR-1138. */

#define N_SCORES 4u

static const char *const score_names[N_SCORES] = {"float_ssim", "float_ssim_l", "float_ssim_c",
                                                  "float_ssim_s"};

/* The header's bytes are planar Y, then U, then V of one 4:2:0 frame. */
static int frame_picture(VmafPicture *pic, const unsigned char *bytes)
{
    const int err = vmaf_picture_alloc(pic, VMAF_PIX_FMT_YUV420P, 8u, FLOAT_SSIM_ORDER_FRAME_W,
                                       FLOAT_SSIM_ORDER_FRAME_H);
    if (err)
        return err;
    const unsigned char *src = bytes;
    for (unsigned p = 0; p < 3u; p++) {
        for (unsigned row = 0; row < pic->h[p]; row++) {
            uint8_t *line = (uint8_t *)pic->data[p] + (size_t)row * pic->stride[p];
            memcpy(line, src, pic->w[p]);
            src += pic->w[p];
        }
    }
    return 0;
}

static int feed_frame(VmafContext *vmaf)
{
    VmafPicture ref;
    VmafPicture dist;
    int err = frame_picture(&ref, float_ssim_order_frame_ref);
    if (err)
        return err;
    err = frame_picture(&dist, float_ssim_order_frame_dis);
    if (err) {
        (void)vmaf_picture_unref(&ref);
        return err;
    }
    err = vmaf_read_pictures(vmaf, &ref, &dist, 0u);
    if (err)
        return err;
    return vmaf_read_pictures(vmaf, NULL, NULL, 0u);
}

static VmafCudaState *open_device(void)
{
    VmafCudaState *cu_state = NULL;
    VmafCudaConfiguration cuda_cfg = {0};
    if (vmaf_cuda_state_init(&cu_state, cuda_cfg) != 0)
        return NULL;
    return cu_state;
}

/* The bit pattern of the float the extractor published as a double. */
static uint32_t float_bits(double score)
{
    const float narrowed = (float)score;
    uint32_t bits = 0u;
    memcpy(&bits, &narrowed, sizeof(bits));
    return bits;
}

/* A context that runs `extractor`, on a fresh CUDA state when `on_device`. */
static char *open_context(bool on_device, const char *extractor, bool enable_lcs,
                          VmafContext **vmaf, VmafCudaState **cu_state)
{
    VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    *cu_state = on_device ? open_device() : NULL;
    mu_assert("CUDA state init failed", *cu_state || !on_device);
    mu_assert("vmaf_init failed", !vmaf_init(vmaf, cfg));
    if (*cu_state) {
        mu_assert("vmaf_cuda_import_state failed", !vmaf_cuda_import_state(*vmaf, *cu_state));
    }
    VmafFeatureDictionary *opts = NULL;
    if (enable_lcs) {
        mu_assert("option set failed", !vmaf_feature_dictionary_set(&opts, "enable_lcs", "true"));
    }
    mu_assert("vmaf_use_feature failed", !vmaf_use_feature(*vmaf, extractor, opts));
    return NULL;
}

/* bits[k] follows score_names, the first `n_scores` of them. */
static char *read_scores(VmafContext *vmaf, unsigned n_scores, uint32_t bits[N_SCORES])
{
    for (unsigned k = 0; k < n_scores; k++) {
        double score = 0.0;
        mu_assert("score missing", !vmaf_feature_score_at_index(vmaf, score_names[k], &score, 0u));
        bits[k] = float_bits(score);
    }
    return NULL;
}

/* Scores the frame pair with `extractor`; the device run gets its own CUDA
 * state, as test_cuda_twin_option_parity.c does. */
static char *score_frame(bool on_device, const char *extractor, bool enable_lcs,
                         uint32_t bits[N_SCORES])
{
    VmafContext *vmaf = NULL;
    VmafCudaState *cu_state = NULL;
    mu_assert_msg(open_context(on_device, extractor, enable_lcs, &vmaf, &cu_state));
    mu_assert("scoring the frame failed", !feed_frame(vmaf));
    mu_assert_msg(read_scores(vmaf, enable_lcs ? N_SCORES : 1u, bits));
    mu_assert("vmaf_close failed", !vmaf_close(vmaf));
    if (cu_state) {
        mu_assert("vmaf_cuda_state_free failed", !vmaf_cuda_state_free(cu_state));
    }
    return NULL;
}

static char *compare_twin(bool enable_lcs)
{
    uint32_t cpu[N_SCORES] = {0u};
    uint32_t gpu[N_SCORES] = {0u};
    mu_assert_msg(score_frame(false, "float_ssim", enable_lcs, cpu));
    mu_assert_msg(score_frame(true, "float_ssim_cuda", enable_lcs, gpu));
    const unsigned n_scores = enable_lcs ? N_SCORES : 1u;
    for (unsigned k = 0; k < n_scores; k++) {
        if (cpu[k] != gpu[k]) {
            (void)fprintf(stderr, "\n%s (enable_lcs=%d): cpu=0x%08x cuda=0x%08x\n", score_names[k],
                          (int)enable_lcs, (unsigned)cpu[k], (unsigned)gpu[k]);
        }
    }
    mu_assert("the fixture no longer scores 0xb4e2b622 on the CPU float_ssim",
              cpu[0] == FLOAT_SSIM_ORDER_FRAME_CPU_BITS);
    for (unsigned k = 0; k < n_scores; k++) {
        mu_assert("float_ssim_cuda must return the CPU's bits on the order frame",
                  cpu[k] == gpu[k]);
    }
    return NULL;
}

static char *test_float_ssim_cuda_adds_in_raster_order(void)
{
    VmafCudaState *cu_state = open_device();
    if (!cu_state) {
        (void)fprintf(stderr, "[skip: no CUDA device] ");
        mu_skipped = 1;
        return NULL;
    }
    mu_assert("vmaf_cuda_state_free failed", !vmaf_cuda_state_free(cu_state));
    mu_assert_msg(compare_twin(false));
    mu_assert_msg(compare_twin(true));
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_float_ssim_cuda_adds_in_raster_order);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
