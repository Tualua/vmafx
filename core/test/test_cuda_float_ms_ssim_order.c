/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * `float_ms_ssim_cuda` adds its per-scale l / c / s terms in the CPU's raster
 * order (T-CUDA-FLOAT-MS-SSIM-FRAME-SUM-ORDER-2026-10-02).
 *
 * iqa/ssim_tools.c::iqa_ssim() adds every window's l, c and s into one double
 * each, left to right and top to bottom, and returns each mean as a float;
 * ms_ssim.c runs it once per scale. The twin computed the same terms and
 * added them per warp, per block and then on the host. A sum of doubles is its
 * order, and on two frame pairs the two orders end on different sides of a
 * float rounding boundary:
 *
 *   - the pair of float_ms_ssim_order_frame.h, shared with the HIP and SYCL
 *     twin tests: `float_ms_ssim_c_scale1` is 0x3f7c49a0 on the CPU and was
 *     0x3f7c499f on the twin, which moves `float_ms_ssim` by 1.3e-9;
 *   - a pair this file rebuilds from a formula (formula_luma()), found on
 *     CUDA: `float_ms_ssim_l_scale0` is 0x3f7cd999 on the CPU and was
 *     0x3f7cd998 on the twin. It exercises another sum at another scale.
 *
 * The test scores each pair on the CPU extractor and on the twin with
 * `enable_lcs`, and requires
 *   - the CPU's named mean to have the recorded bits, so the fixture is still
 *     the frame it was found to be on this build, and
 *   - all sixteen outputs of the twin to equal the CPU's.
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

#include "float_ms_ssim_order_frame.h"

#include "libvmaf/feature.h"
#include "libvmaf/libvmaf.h"
#include "libvmaf/libvmaf_cuda.h"
#include "libvmaf/picture.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy also proposes the `nullptr` keyword, but MSVC's
 * documented /std:clatest C23 feature set does not include `nullptr` while the
 * required Windows build compiles this TU with cl.exe, and this file mirrors
 * the C spelling of the surface it exercises. ADR-1138. */

#define N_SCALES 5u
/* The score, then l, c and s of every scale. */
#define N_SCORES (1u + (3u * N_SCALES))
#define NAME_BYTES 40u
#define CHROMA_VALUE 128

/* The formula pair: frame 12 376 132 of a family of noise frames whose luma
 * sample i, in raster order, is the top byte of
 * splitmix64(((2 * frame + side) << 32) | i), side 0 for the reference and 1
 * for the distorted picture. The one frame with a difference among 1.86
 * million of that family scored on the CPU and on the CUDA twin. */
#define FORMULA_FRAME UINT64_C(12376132)
#define FORMULA_KEY "float_ms_ssim_l_scale0"
#define FORMULA_CPU_BITS 0x3f7cd999u

/* One frame pair: where its luma comes from and which CPU mean identifies
 * it. */
typedef struct OrderCase {
    const char *label;
    void (*luma)(unsigned side, uint8_t *dst);
    const char *key;
    uint32_t cpu_bits;
} OrderCase;

static void score_name(unsigned k, char name[NAME_BYTES])
{
    static const char terms[3] = {'l', 'c', 's'};
    if (k == 0u) {
        (void)snprintf(name, NAME_BYTES, "float_ms_ssim");
        return;
    }
    const unsigned term = (k - 1u) / N_SCALES;
    const unsigned scale = (k - 1u) % N_SCALES;
    (void)snprintf(name, NAME_BYTES, "float_ms_ssim_%c_scale%u", terms[term], scale);
}

/* The header's luma planes, FLOAT_MS_SSIM_ORDER_LUMA_BYTES each. */
static void shared_luma(unsigned side, uint8_t *dst)
{
    memcpy(dst, side ? float_ms_ssim_order_dis_luma : float_ms_ssim_order_ref_luma,
           FLOAT_MS_SSIM_ORDER_LUMA_BYTES);
}

static void formula_luma(unsigned side, uint8_t *dst)
{
    for (uint32_t i = 0; i < (uint32_t)FLOAT_MS_SSIM_ORDER_LUMA_BYTES; i++) {
        uint64_t z = (((UINT64_C(2) * FORMULA_FRAME) + (uint64_t)side) << 32) | (uint64_t)i;
        z += UINT64_C(0x9E3779B97F4A7C15);
        z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
        z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
        z ^= z >> 31;
        dst[i] = (uint8_t)(z >> 56);
    }
}

/* One picture of a pair: its luma plane row by row, flat chroma (float_ms_ssim
 * scores luma only). */
static int frame_picture(VmafPicture *pic, const OrderCase *oc, unsigned side)
{
    static uint8_t luma[FLOAT_MS_SSIM_ORDER_LUMA_BYTES];
    const int err = vmaf_picture_alloc(pic, VMAF_PIX_FMT_YUV420P, 8u, FLOAT_MS_SSIM_ORDER_W,
                                       FLOAT_MS_SSIM_ORDER_H);
    if (err)
        return err;
    oc->luma(side, luma);
    for (unsigned row = 0; row < pic->h[0]; row++) {
        uint8_t *line = (uint8_t *)pic->data[0] + (size_t)row * pic->stride[0];
        memcpy(line, luma + (size_t)row * pic->w[0], pic->w[0]);
    }
    for (unsigned p = 1; p < 3u; p++) {
        for (unsigned row = 0; row < pic->h[p]; row++) {
            uint8_t *line = (uint8_t *)pic->data[p] + (size_t)row * pic->stride[p];
            memset(line, CHROMA_VALUE, pic->w[p]);
        }
    }
    return 0;
}

static int feed_frame(VmafContext *vmaf, const OrderCase *oc)
{
    VmafPicture ref;
    VmafPicture dist;
    int err = frame_picture(&ref, oc, 0u);
    if (err)
        return err;
    err = frame_picture(&dist, oc, 1u);
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

/* The bit pattern of a published score. */
static uint64_t double_bits(double score)
{
    uint64_t bits = 0u;
    memcpy(&bits, &score, sizeof(bits));
    return bits;
}

/* The bit pattern of the float a per-scale mean was published from. */
static uint32_t float_bits(double score)
{
    const float narrowed = (float)score;
    uint32_t bits = 0u;
    memcpy(&bits, &narrowed, sizeof(bits));
    return bits;
}

/* A context that runs `extractor` with enable_lcs, on a fresh CUDA state when
 * `on_device`. */
static char *open_context(bool on_device, const char *extractor, VmafContext **vmaf,
                          VmafCudaState **cu_state)
{
    VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    *cu_state = on_device ? open_device() : NULL;
    mu_assert("CUDA state init failed", *cu_state || !on_device);
    mu_assert("vmaf_init failed", !vmaf_init(vmaf, cfg));
    if (*cu_state) {
        mu_assert("vmaf_cuda_import_state failed", !vmaf_cuda_import_state(*vmaf, *cu_state));
    }
    VmafFeatureDictionary *opts = NULL;
    mu_assert("option set failed", !vmaf_feature_dictionary_set(&opts, "enable_lcs", "true"));
    mu_assert("vmaf_use_feature failed", !vmaf_use_feature(*vmaf, extractor, opts));
    return NULL;
}

static char *read_scores(VmafContext *vmaf, double scores[N_SCORES])
{
    for (unsigned k = 0; k < N_SCORES; k++) {
        char name[NAME_BYTES];
        score_name(k, name);
        mu_assert("score missing", !vmaf_feature_score_at_index(vmaf, name, &scores[k], 0u));
    }
    return NULL;
}

static char *score_frame(bool on_device, const char *extractor, const OrderCase *oc,
                         double scores[N_SCORES])
{
    VmafContext *vmaf = NULL;
    VmafCudaState *cu_state = NULL;
    mu_assert_msg(open_context(on_device, extractor, &vmaf, &cu_state));
    mu_assert("scoring the frame failed", !feed_frame(vmaf, oc));
    mu_assert_msg(read_scores(vmaf, scores));
    mu_assert("vmaf_close failed", !vmaf_close(vmaf));
    if (cu_state) {
        mu_assert("vmaf_cuda_state_free failed", !vmaf_cuda_state_free(cu_state));
    }
    return NULL;
}

static char *compare_case(const OrderCase *oc)
{
    double cpu[N_SCORES] = {0.0};
    double gpu[N_SCORES] = {0.0};
    mu_assert_msg(score_frame(false, "float_ms_ssim", oc, cpu));
    mu_assert_msg(score_frame(true, "float_ms_ssim_cuda", oc, gpu));
    double recorded = 0.0;
    for (unsigned k = 0; k < N_SCORES; k++) {
        char name[NAME_BYTES];
        score_name(k, name);
        if (!strcmp(name, oc->key)) {
            recorded = cpu[k];
        }
        if (double_bits(cpu[k]) != double_bits(gpu[k])) {
            (void)fprintf(stderr, "\n%s pair, %s: cpu=%.17g (0x%08x) cuda=%.17g (0x%08x)\n",
                          oc->label, name, cpu[k], (unsigned)float_bits(cpu[k]), gpu[k],
                          (unsigned)float_bits(gpu[k]));
        }
    }
    mu_assert("a fixture no longer has its recorded bits on the CPU float_ms_ssim",
              float_bits(recorded) == oc->cpu_bits);
    for (unsigned k = 0; k < N_SCORES; k++) {
        mu_assert("float_ms_ssim_cuda must return the CPU's bits on the order frames",
                  double_bits(cpu[k]) == double_bits(gpu[k]));
    }
    return NULL;
}

static char *test_float_ms_ssim_cuda_adds_in_raster_order(void)
{
    static const OrderCase cases[] = {
        {"shared", shared_luma, FLOAT_MS_SSIM_ORDER_KEY, FLOAT_MS_SSIM_ORDER_CPU_BITS},
        {"formula", formula_luma, FORMULA_KEY, FORMULA_CPU_BITS},
    };
    VmafCudaState *cu_state = open_device();
    if (!cu_state) {
        (void)fprintf(stderr, "[skip: no CUDA device] ");
        mu_skipped = 1;
        return NULL;
    }
    mu_assert("vmaf_cuda_state_free failed", !vmaf_cuda_state_free(cu_state));
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        mu_assert_msg(compare_case(&cases[i]));
    }
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_float_ms_ssim_cuda_adds_in_raster_order);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
