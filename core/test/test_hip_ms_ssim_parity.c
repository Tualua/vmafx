/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * ADR-0883 round-2 — MS-SSIM CPU vs. HIP parity test.
 *
 * Multi-Scale SSIM is computed by float_ms_ssim.c (CPU) and by
 * integer_ms_ssim_hip.c + integer_ms_ssim/ms_ssim_score.hip (HIP).
 * The HIP path has no cross-backend assertion before this test; a
 * regression in the per-scale Gaussian pyramid + multi-scale product
 * would silently shift downstream metrics.
 *
 * Asserts the single emitted `float_ms_ssim` channel.  Tolerance is
 * places=3 (1e-3) — multi-scale reduction amplifies per-window
 * rounding more than single-scale SSIM, so we use the same budget
 * VIF gets per ADR-0214.
 *
 * Since ADR-1403 the twin is the CPU's arithmetic, and
 * test_ms_ssim_matches_cpu_bit_for_bit holds every output of every frame,
 * the 15 per-scale l / c / s means of `enable_lcs` included, to the CPU's
 * value bit for bit. The tolerance tests above it stay as the coarse gate
 * for the dB options.
 *
 * The per-scale sums are the CPU's as well: ssim_accumulate_default_scalar()
 * adds the l, c and s terms of a scale into one double each in raster order,
 * every add rounds, and the twin reads the terms back and adds them in that
 * order. A sum of the same terms in another order is another double, and on
 * the constructed pair of float_ms_ssim_order_frame.h one mean rounds to the
 * neighbouring float (T-GPU-FLOAT-SSIM-FRAME-SUM-ORDER-2026-10-02): the twin
 * returned 0x3f7c499f for float_ms_ssim_c_scale1 there while it added per
 * wave and block, the CPU returns 0x3f7c49a0.
 * test_ms_ssim_frame_sum_order holds the twin to the CPU's bits on that pair.
 *
 * With enable_chroma the twin scores Cb and Cr as the CPU does; it used to
 * accept the option and drop both outputs
 * (T-MS-SSIM-GPU-CHROMA-OPTION-DRIFT-2026-09-06). The chroma tests compare
 * every output on 4:2:0, 4:2:2 and 4:4:4 frames with `==` and hold the
 * twin's refusals and YUV400P scores to the CPU's.
 *
 * Skip behaviour: if vmaf_hip_state_init() fails (no HIP runtime or
 * no device visible) the test emits "[skip: no HIP device]" and passes.
 */

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test.h"

#include "float_ms_ssim_order_frame.h"

#include "feature/feature_extractor.h"
#include "libvmaf/libvmaf.h"
#include "libvmaf/libvmaf_hip.h"
#include "libvmaf/picture.h"

/* MS-SSIM has a 5-level 11-tap Gaussian pyramid; each downscale halves the
 * dimensions.  The minimum admissible dimension is GAUSSIAN_LEN << (SCALES-1)
 * = 11 << 4 = 176 (float_ms_ssim.c:131).  256x192 satisfies both axes with
 * margin and keeps the smallest scale at 16x12 — above the 11x11 window.
 * A fixture shorter than 176 px returns -EINVAL from vmaf_read_pictures. */
#ifndef FIXTURE_W
#define FIXTURE_W 256u
#endif
#ifndef FIXTURE_H
#define FIXTURE_H 192u
#endif
#define FIXTURE_BPC 8u
#define PARITY_TOL 1e-3

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy also proposes the `nullptr` keyword, but MSVC's
 * documented /std:clatest C23 feature set does not include `nullptr` while the
 * required Windows build compiles this TU with cl.exe, and this file mirrors
 * the C spelling of the surface it exercises. ADR-1138. */

static int fill_pic(VmafPicture *pic, unsigned salt)
{
    int err = vmaf_picture_alloc(pic, VMAF_PIX_FMT_YUV420P, FIXTURE_BPC, FIXTURE_W, FIXTURE_H);
    if (err)
        return err;
    uint8_t *y = (uint8_t *)pic->data[0];
    for (unsigned row = 0; row < pic->h[0]; row++) {
        for (unsigned col = 0; col < pic->w[0]; col++) {
            y[row * pic->stride[0] + col] = (uint8_t)((row + col + salt * 19u) & 0xFFu);
        }
    }
    for (unsigned p = 1; p < 3; p++) {
        uint8_t *plane = (uint8_t *)pic->data[p];
        for (unsigned row = 0; row < pic->h[p]; row++) {
            memset(plane + row * pic->stride[p], 128, pic->w[p]);
        }
    }
    return 0;
}

static int feed_frame(VmafContext *vmaf, bool identical)
{
    VmafPicture ref;
    VmafPicture dist;
    int err = fill_pic(&ref, 0u);
    if (err)
        return err;
    /* An identical pair drives ms_ssim to 1.0, which is where the ADR-1221
     * dB ceiling actually binds. */
    err = fill_pic(&dist, identical ? 0u : 1u);
    if (err) {
        (void)vmaf_picture_unref(&ref);
        return err;
    }
    return vmaf_read_pictures(vmaf, &ref, &dist, 0u);
}

/* ADR-1221 — `enable_db` / `clip_db` opt into the dB-domain score with a
 * geometry-derived ceiling. Neither is a VMAF_OPT_FLAG_FEATURE_PARAM, so the
 * collector key stays `float_ms_ssim`. */
static int ms_ssim_db_opts(VmafFeatureDictionary **opts)
{
    int err = vmaf_feature_dictionary_set(opts, "enable_db", "true");
    if (err)
        return err;
    return vmaf_feature_dictionary_set(opts, "clip_db", "true");
}

/* The device, or NULL with the reason printed when there is none. */
static VmafHipState *ms_hip_state(void)
{
    VmafHipState *hip_state = NULL;
    VmafHipConfiguration hip_cfg = {.device_index = -1};
    if (vmaf_hip_state_init(&hip_state, hip_cfg) != 0 || hip_state == NULL) {
        (void)fprintf(stderr, "[skip: no HIP device] ");
        return NULL;
    }
    return hip_state;
}

/* A context with one MS-SSIM extractor: `integer_ms_ssim_hip` on `hip_state`,
 * or the CPU's `float_ms_ssim` when it is NULL. `opts` is consumed. */
static int ms_context_new(VmafContext **vmaf, VmafHipState *hip_state, VmafFeatureDictionary *opts)
{
    VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    int err = vmaf_init(vmaf, cfg);
    if (!err && hip_state)
        err = vmaf_hip_import_state(*vmaf, hip_state);
    if (!err) {
        err = vmaf_use_feature(*vmaf, hip_state ? "integer_ms_ssim_hip" : "float_ms_ssim", opts);
        opts = NULL; /* consumed on every path (libvmaf.h) */
    }
    if (opts)
        (void)vmaf_feature_dictionary_free(&opts);
    return err;
}

/* `float_ms_ssim` of one frame pair from one extractor. */
static int ms_single_score(VmafHipState *hip_state, bool db, bool identical, double *score)
{
    VmafFeatureDictionary *opts = NULL;
    int err = db ? ms_ssim_db_opts(&opts) : 0;
    VmafContext *vmaf = NULL;
    if (err) {
        (void)vmaf_feature_dictionary_free(&opts);
        return err;
    }
    err = ms_context_new(&vmaf, hip_state, opts);
    if (!err)
        err = feed_frame(vmaf, identical);
    if (!err)
        err = vmaf_read_pictures(vmaf, NULL, NULL, 0);
    if (!err)
        err = vmaf_feature_score_at_index(vmaf, "float_ms_ssim", score, 0u);
    const int closed = vmaf ? vmaf_close(vmaf) : 0;
    return err ? err : closed;
}

/* The CPU's and the twin's score of one frame pair. `*skipped` is set, and 0
 * returned, when there is no device or the twin is a scaffold: an
 * unimplemented HIP extractor returns -ENOSYS (see the HIP extractors under
 * core/src/feature/hip/), which is a not-built-yet signal, not a regression.
 * Any other error is returned. */
static int ms_pair_scores(bool db, bool identical, double *cpu, double *gpu, bool *skipped)
{
    *skipped = true;
    VmafHipState *hip_state = ms_hip_state();
    if (!hip_state)
        return 0;
    const int gpu_err = ms_single_score(hip_state, db, identical, gpu);
    vmaf_hip_state_free(&hip_state);
    if (gpu_err == -ENOSYS) {
        (void)fprintf(stderr, "[skip: integer_ms_ssim_hip is a scaffold (-ENOSYS)] ");
        return 0;
    }
    if (gpu_err)
        return gpu_err;
    *skipped = false;
    return ms_single_score(NULL, db, identical, cpu);
}

static char *test_ms_ssim_hip_registered(void)
{
    VmafFeatureExtractor *fex = vmaf_get_feature_extractor_by_name("integer_ms_ssim_hip");
    mu_assert("integer_ms_ssim_hip extractor must be registered", fex != NULL);
    mu_assert("integer_ms_ssim_hip name matches", !strcmp(fex->name, "integer_ms_ssim_hip"));
    return NULL;
}

static char *test_ms_ssim_cpu_hip_parity(void)
{
    double cpu = 0.0;
    double gpu = NAN;
    bool skipped = false;
    mu_assert("float_ms_ssim: the CPU or the HIP run failed",
              ms_pair_scores(false, false, &cpu, &gpu, &skipped) == 0);
    if (skipped)
        return NULL;
    const double delta = fabs(cpu - gpu);
    if (!(delta <= PARITY_TOL)) {
        (void)fprintf(stderr, "\nms_ssim parity FAIL: cpu=%.8f hip=%.8f delta=%.2e tol=%.2e\n", cpu,
                      gpu, delta, PARITY_TOL);
    }
    mu_assert("float_ms_ssim CPU vs. HIP delta exceeds places=3 tolerance (1e-3)",
              delta <= PARITY_TOL);
    return NULL;
}

/* ADR-1221 — clip_db is a CEILING on the dB output, not a clamp on the linear
 * score. float_ms_ssim.c derives `max_db = ceil(10*log10(peak*peak/mse))` with
 * `mse = 0.5/(w*h)` and returns `MIN(-10*log10(1 - score), max_db)`,
 * short-circuiting to `max_db` when score >= 1.0. This twin used to clamp the
 * LINEAR score into [0, 1] and then convert with no ceiling, which returns
 * +Inf on an identical reference/distorted pair — an ordinary thing to score.
 * The default-options test above cannot see it: with enable_db off, neither
 * path converts at all. An identical pair drives ms_ssim to 1.0, which is
 * where the ceiling binds. */
static char *test_ms_ssim_clip_db_ceiling(void)
{
    double cpu = 0.0;
    double gpu = NAN;
    bool skipped = false;
    mu_assert("float_ms_ssim enable_db+clip_db: the CPU or the HIP run failed",
              ms_pair_scores(true, true, &cpu, &gpu, &skipped) == 0);
    if (skipped)
        return NULL;

    mu_assert("CPU float_ms_ssim dB score is non-finite", isfinite(cpu));
    mu_assert("HIP float_ms_ssim dB score is non-finite -- clip_db must cap it at max_db",
              isfinite(gpu));

    const double delta = fabs(cpu - gpu);
    if (!(delta <= PARITY_TOL)) {
        (void)fprintf(stderr,
                      "\nfloat_ms_ssim enable_db+clip_db parity FAIL: cpu=%.8f hip=%.8f "
                      "delta=%.2e tol=%.2e\n",
                      cpu, gpu, delta, PARITY_TOL);
    }
    mu_assert("float_ms_ssim dB score drifts from the CPU reference", delta <= PARITY_TOL);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* ADR-1403 — float_ms_ssim_hip is the CPU's arithmetic, bit for bit.    */
/*                                                                     */
/* The kernels compute each sample through integer_ms_ssim/             */
/* ms_ssim_arith.h: one fused multiply-add per decimate tap             */
/* (ms_ssim_decimate.c), fp32 products summed as an exact fp32 pair     */
/* that stands for iqa_convolve()'s fp64 sum, and the fp32 denominators */
/* and quotient of ssim_accumulate_default_scalar(). The host rounds    */
/* each per-scale mean to fp32 and combines as ms_ssim.c does. Before   */
/* that the twin ran fp32 running sums, fp64 denominators and unrounded */
/* means and was 5e-8 to 3e-6 from the CPU on a gfx1036, on every frame */
/* of the Netflix pair, the 1080p checkerboards and BBB 3840x2160.      */
/* ------------------------------------------------------------------ */
#define MS_EXACT_KEYS 16u
#define MS_EXACT_FRAMES 3u

typedef struct MsExactScores {
    double v[MS_EXACT_FRAMES][MS_EXACT_KEYS];
} MsExactScores;

static const char *const ms_exact_keys[MS_EXACT_KEYS] = {
    "float_ms_ssim",          "float_ms_ssim_l_scale0", "float_ms_ssim_l_scale1",
    "float_ms_ssim_l_scale2", "float_ms_ssim_l_scale3", "float_ms_ssim_l_scale4",
    "float_ms_ssim_c_scale0", "float_ms_ssim_c_scale1", "float_ms_ssim_c_scale2",
    "float_ms_ssim_c_scale3", "float_ms_ssim_c_scale4", "float_ms_ssim_s_scale0",
    "float_ms_ssim_s_scale1", "float_ms_ssim_s_scale2", "float_ms_ssim_s_scale3",
    "float_ms_ssim_s_scale4",
};

/* A textured reference and a dimmed, blocked copy of it, so l, c and s all
 * leave 1 at every scale; fill_pic()'s ramp alone leaves most of them at 1. */
static int ms_exact_fill(VmafPicture *pic, unsigned frame, bool distorted)
{
    const int err = fill_pic(pic, frame);
    if (err) {
        return err;
    }
    uint32_t state = 0x9E3779B9u ^ (frame * 2654435761u);
    uint8_t *y = (uint8_t *)pic->data[0];
    for (unsigned row = 0; row < pic->h[0]; row++) {
        for (unsigned col = 0; col < pic->w[0]; col++) {
            state = (state * 1664525u) + 1013904223u;
            unsigned value = (((col * 5u) + (row * 3u) + (frame * 11u)) & 127u) + 64u;
            value += (state >> 8) & 31u;
            if (distorted) {
                value = value - (value / 9u) + ((((col / 8u) ^ (row / 8u)) & 1u) * 6u);
            }
            y[(row * pic->stride[0]) + col] = (uint8_t)value;
        }
    }
    return 0;
}

static int ms_exact_feed(VmafContext *vmaf)
{
    for (unsigned i = 0; i < MS_EXACT_FRAMES; i++) {
        VmafPicture ref;
        VmafPicture dist;
        int err = ms_exact_fill(&ref, i, false);
        if (err) {
            return err;
        }
        err = ms_exact_fill(&dist, i, true);
        if (err) {
            (void)vmaf_picture_unref(&ref);
            return err;
        }
        err = vmaf_read_pictures(vmaf, &ref, &dist, i);
        if (err) {
            return err;
        }
    }
    return vmaf_read_pictures(vmaf, NULL, NULL, 0);
}

static int ms_exact_collect(VmafContext *vmaf, MsExactScores *out)
{
    for (unsigned k = 0; k < MS_EXACT_KEYS; k++) {
        for (unsigned i = 0; i < MS_EXACT_FRAMES; i++) {
            const int err = vmaf_feature_score_at_index(vmaf, ms_exact_keys[k], &out->v[i][k], i);
            if (err) {
                return err;
            }
        }
    }
    return 0;
}

/* Every frame's 16 outputs from one extractor; `hip_state` NULL runs the CPU. */
static int ms_exact_score(VmafHipState *hip_state, MsExactScores *out)
{
    VmafFeatureDictionary *opts = NULL;
    VmafContext *vmaf = NULL;
    int err = vmaf_feature_dictionary_set(&opts, "enable_lcs", "true");
    if (err) {
        (void)vmaf_feature_dictionary_free(&opts);
        return err;
    }
    err = ms_context_new(&vmaf, hip_state, opts);
    if (!err) {
        err = ms_exact_feed(vmaf);
    }
    if (!err) {
        err = ms_exact_collect(vmaf, out);
    }
    const int closed = vmaf ? vmaf_close(vmaf) : 0;
    return err ? err : closed;
}

static uint64_t ms_exact_bits(double v)
{
    uint64_t bits = 0u;
    memcpy(&bits, &v, sizeof(bits));
    return bits;
}

/* The outputs that are not the CPU's bit for bit, each one reported. */
static unsigned ms_exact_mismatches(const MsExactScores *cpu, const MsExactScores *gpu)
{
    unsigned differing = 0u;
    for (unsigned i = 0; i < MS_EXACT_FRAMES; i++) {
        for (unsigned k = 0; k < MS_EXACT_KEYS; k++) {
            if (ms_exact_bits(cpu->v[i][k]) == ms_exact_bits(gpu->v[i][k])) {
                continue;
            }
            differing++;
            (void)fprintf(stderr, "\n%s frame %u: cpu=%.17g hip=%.17g delta=%.3e", ms_exact_keys[k],
                          i, cpu->v[i][k], gpu->v[i][k], fabs(cpu->v[i][k] - gpu->v[i][k]));
        }
    }
    return differing;
}

static char *test_ms_ssim_matches_cpu_bit_for_bit(void)
{
    VmafHipState *hip_state = ms_hip_state();
    if (!hip_state)
        return NULL;
    MsExactScores cpu;
    MsExactScores gpu;
    memset(&cpu, 0, sizeof(cpu));
    memset(&gpu, 0, sizeof(gpu));
    const int gpu_err = ms_exact_score(hip_state, &gpu);
    vmaf_hip_state_free(&hip_state);
    if (gpu_err == -ENOSYS) {
        (void)fprintf(stderr, "[skip: integer_ms_ssim_hip is a scaffold (-ENOSYS)] ");
        return NULL;
    }
    mu_assert("HIP: integer_ms_ssim_hip with enable_lcs failed", gpu_err == 0);
    mu_assert("CPU: float_ms_ssim with enable_lcs failed", ms_exact_score(NULL, &cpu) == 0);
    mu_assert("CPU float_ms_ssim is not a usable reference",
              isfinite(cpu.v[1][0]) && cpu.v[1][0] > 0.0 && cpu.v[1][0] < 1.0);
    mu_assert("float_ms_ssim_hip is not the CPU extractor's value bit for bit",
              ms_exact_mismatches(&cpu, &gpu) == 0u);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* T-GPU-FLOAT-SSIM-FRAME-SUM-ORDER-2026-10-02 — the per-scale sums in   */
/* the CPU's raster order.                                              */
/* ------------------------------------------------------------------ */

/* One picture of float_ms_ssim_order_frame.h: the stored luma plane, and
 * chroma at 128 (float_ms_ssim scores luma only). */
static int order_frame_picture(VmafPicture *pic, const unsigned char *luma)
{
    int err = vmaf_picture_alloc(pic, VMAF_PIX_FMT_YUV420P, 8u, FLOAT_MS_SSIM_ORDER_W,
                                 FLOAT_MS_SSIM_ORDER_H);
    if (err) {
        return err;
    }
    for (unsigned row = 0; row < pic->h[0]; row++) {
        memcpy((uint8_t *)pic->data[0] + ((size_t)row * pic->stride[0]),
               luma + ((size_t)row * pic->w[0]), pic->w[0]);
    }
    for (unsigned p = 1; p < 3u; p++) {
        for (unsigned row = 0; row < pic->h[p]; row++) {
            memset((uint8_t *)pic->data[p] + ((size_t)row * pic->stride[p]), 128, pic->w[p]);
        }
    }
    return 0;
}

static int order_frame_feed(VmafContext *vmaf)
{
    VmafPicture ref;
    VmafPicture dist;
    int err = order_frame_picture(&ref, float_ms_ssim_order_ref_luma);
    if (err) {
        return err;
    }
    err = order_frame_picture(&dist, float_ms_ssim_order_dis_luma);
    if (err) {
        (void)vmaf_picture_unref(&ref);
        return err;
    }
    err = vmaf_read_pictures(vmaf, &ref, &dist, 0u);
    return err ? err : vmaf_read_pictures(vmaf, NULL, NULL, 0);
}

/* The 16 outputs of the constructed pair from one extractor; `hip_state`
 * NULL runs the CPU. */
static int order_frame_scores(VmafHipState *hip_state, double out[MS_EXACT_KEYS])
{
    VmafFeatureDictionary *opts = NULL;
    VmafContext *vmaf = NULL;
    int err = vmaf_feature_dictionary_set(&opts, "enable_lcs", "true");
    if (err) {
        (void)vmaf_feature_dictionary_free(&opts);
        return err;
    }
    err = ms_context_new(&vmaf, hip_state, opts);
    if (!err) {
        err = order_frame_feed(vmaf);
    }
    for (unsigned k = 0; !err && k < MS_EXACT_KEYS; k++) {
        err = vmaf_feature_score_at_index(vmaf, ms_exact_keys[k], &out[k], 0u);
    }
    const int closed = vmaf ? vmaf_close(vmaf) : 0;
    return err ? err : closed;
}

/* The bits of the `float` behind a per-scale mean. The mean is a float
 * widened to double, so the narrowing is exact. */
static uint32_t order_frame_float_bits(double mean)
{
    const float narrowed = (float)mean;
    uint32_t bits = 0u;
    memcpy(&bits, &narrowed, sizeof(bits));
    return bits;
}

/* The outputs of the constructed pair that are not the CPU's, each reported. */
static unsigned order_frame_mismatches(const double *cpu, const double *gpu)
{
    unsigned differing = 0u;
    for (unsigned k = 0; k < MS_EXACT_KEYS; k++) {
        if (ms_exact_bits(cpu[k]) == ms_exact_bits(gpu[k])) {
            continue;
        }
        differing++;
        (void)fprintf(stderr, "\n%s: cpu=%.17g (0x%08x) hip=%.17g (0x%08x)", ms_exact_keys[k],
                      cpu[k], order_frame_float_bits(cpu[k]), gpu[k],
                      order_frame_float_bits(gpu[k]));
    }
    return differing;
}

/* On this pair the mean of scale 1's contrast terms added per wave and block
 * is the neighbouring float (0x3f7c499f). The CPU's value is checked first:
 * it is the fixture's premise and needs no device. */
static char *test_ms_ssim_frame_sum_order(void)
{
    double cpu[MS_EXACT_KEYS] = {0};
    double gpu[MS_EXACT_KEYS] = {0};
    mu_assert("CPU: float_ms_ssim on the constructed pair failed",
              order_frame_scores(NULL, cpu) == 0);
    uint32_t premise = 0u;
    for (unsigned k = 0; k < MS_EXACT_KEYS; k++) {
        if (!strcmp(ms_exact_keys[k], FLOAT_MS_SSIM_ORDER_KEY)) {
            premise = order_frame_float_bits(cpu[k]);
        }
    }
    if (premise != FLOAT_MS_SSIM_ORDER_CPU_BITS) {
        (void)fprintf(stderr, "\ncpu %s bits 0x%08x, the fixture expects 0x%08x\n",
                      FLOAT_MS_SSIM_ORDER_KEY, premise, FLOAT_MS_SSIM_ORDER_CPU_BITS);
    }
    mu_assert("the CPU float_ms_ssim of the constructed pair is not the fixture's value",
              premise == FLOAT_MS_SSIM_ORDER_CPU_BITS);

    VmafHipState *hip_state = ms_hip_state();
    if (!hip_state) {
        return NULL;
    }
    const int gpu_err = order_frame_scores(hip_state, gpu);
    vmaf_hip_state_free(&hip_state);
    if (gpu_err == -ENOSYS) {
        (void)fprintf(stderr, "[skip: integer_ms_ssim_hip is a scaffold (-ENOSYS)] ");
        return NULL;
    }
    mu_assert("HIP: integer_ms_ssim_hip on the constructed pair failed", gpu_err == 0);
    mu_assert("float_ms_ssim_hip does not add the per-scale sums in the CPU's raster order",
              order_frame_mismatches(cpu, gpu) == 0u);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* T-MS-SSIM-GPU-CHROMA-OPTION-DRIFT-2026-09-06 — enable_chroma.         */
/*                                                                     */
/* float_ms_ssim.c runs its pipeline once per plane and emits           */
/* float_ms_ssim_cb and float_ms_ssim_cr. Until 2026-10-03 this twin    */
/* accepted the option, scored luma only and emitted neither: these     */
/* cases fail on that code with a missing key. Each fixture has its own */
/* texture and distortion per plane, so a twin that scored one plane    */
/* three times, or a chroma plane with luma's row pitch, would differ.  */
/* The 4:2:0 frame has odd dimensions (ceil-subsampled 177x178 chroma), */
/* the 4:2:2 one is 10-bit and the 4:4:4 one has full-size chroma.      */
/* ------------------------------------------------------------------ */
#define MS_CHROMA_FRAMES 3u
#define MS_CHROMA_KEYS (MS_EXACT_KEYS + 2u)

typedef struct MsChromaFixture {
    const char *what;
    enum VmafPixelFormat pix_fmt;
    unsigned w;
    unsigned h;
    unsigned bpc;
} MsChromaFixture;

static const MsChromaFixture ms_chroma_fixtures[] = {
    {"4:2:0 8-bit 353x355", VMAF_PIX_FMT_YUV420P, 353u, 355u, 8u},
    {"4:2:2 10-bit 352x192", VMAF_PIX_FMT_YUV422P, 352u, 192u, 10u},
    {"4:4:4 8-bit 256x192", VMAF_PIX_FMT_YUV444P, 256u, 192u, 8u},
};
#define MS_CHROMA_FIXTURE_COUNT (sizeof(ms_chroma_fixtures) / sizeof(ms_chroma_fixtures[0]))

/* One run: a fixture and the options both extractors get on top of
 * enable_chroma. */
typedef struct MsChromaRun {
    const MsChromaFixture *fx;
    bool lcs;       /* enable_lcs: the 15 luma means as well */
    bool db;        /* enable_db */
    bool clip;      /* clip_db */
    bool identical; /* the distorted picture is the reference */
} MsChromaRun;

typedef struct MsChromaScores {
    double v[MS_CHROMA_FRAMES][MS_CHROMA_KEYS];
} MsChromaScores;

/* Output `k` of a run: the score, the 15 luma means with enable_lcs, then
 * the two chroma scores. */
static const char *ms_chroma_key(const MsChromaRun *run, unsigned k)
{
    const unsigned luma_keys = run->lcs ? MS_EXACT_KEYS : 1u;
    if (k < luma_keys)
        return ms_exact_keys[k];
    return k == luma_keys ? "float_ms_ssim_cb" : "float_ms_ssim_cr";
}

static unsigned ms_chroma_key_count(const MsChromaRun *run)
{
    return (run->lcs ? MS_EXACT_KEYS : 1u) + 2u;
}

/* A textured sample of plane `p`; the distorted copy is dimmed and blocked by
 * an amount that depends on the plane. */
static unsigned ms_chroma_sample(unsigned p, unsigned row, unsigned col, unsigned frame,
                                 bool distorted, uint32_t *state)
{
    *state = (*state * 1664525u) + 1013904223u;
    unsigned v = (((col * (5u + p)) + (row * (3u + (2u * p))) + (frame * 11u)) & 127u) + 64u;
    v += (*state >> 8) & 31u;
    if (distorted)
        v = v - (v / (7u + (2u * p))) + ((((col / 8u) ^ (row / 8u)) & 1u) * (4u + (2u * p)));
    return v;
}

static void ms_chroma_fill_plane(VmafPicture *pic, unsigned p, unsigned frame, bool distorted)
{
    const unsigned shift = pic->bpc - 8u;
    uint32_t state = 0x9E3779B9u ^ (frame * 2654435761u) ^ (p * 40503u);
    for (unsigned row = 0; row < pic->h[p]; row++) {
        uint8_t *line = (uint8_t *)pic->data[p] + ((size_t)row * pic->stride[p]);
        for (unsigned col = 0; col < pic->w[p]; col++) {
            const unsigned low = (state >> 3) & ((1u << shift) - 1u);
            const unsigned v =
                (ms_chroma_sample(p, row, col, frame, distorted, &state) << shift) + low;
            if (pic->bpc <= 8u) {
                line[col] = (uint8_t)v;
            } else {
                ((uint16_t *)line)[col] = (uint16_t)v;
            }
        }
    }
}

static int ms_chroma_picture(VmafPicture *pic, const MsChromaFixture *fx, unsigned frame,
                             bool distorted)
{
    const int err = vmaf_picture_alloc(pic, fx->pix_fmt, fx->bpc, fx->w, fx->h);
    if (err)
        return err;
    const unsigned planes = fx->pix_fmt == VMAF_PIX_FMT_YUV400P ? 1u : 3u;
    for (unsigned p = 0; p < planes; p++)
        ms_chroma_fill_plane(pic, p, frame, distorted);
    return 0;
}

static int ms_chroma_feed(VmafContext *vmaf, const MsChromaRun *run)
{
    for (unsigned i = 0; i < MS_CHROMA_FRAMES; i++) {
        VmafPicture ref;
        VmafPicture dist;
        int err = ms_chroma_picture(&ref, run->fx, i, false);
        if (err)
            return err;
        err = ms_chroma_picture(&dist, run->fx, i, !run->identical);
        if (err) {
            (void)vmaf_picture_unref(&ref);
            return err;
        }
        err = vmaf_read_pictures(vmaf, &ref, &dist, i);
        if (err)
            return err;
    }
    return vmaf_read_pictures(vmaf, NULL, NULL, 0);
}

static int ms_chroma_opts(const MsChromaRun *run, VmafFeatureDictionary **opts)
{
    int err = vmaf_feature_dictionary_set(opts, "enable_chroma", "true");
    if (!err && run->lcs)
        err = vmaf_feature_dictionary_set(opts, "enable_lcs", "true");
    if (!err && run->db)
        err = vmaf_feature_dictionary_set(opts, "enable_db", "true");
    if (!err && run->clip)
        err = vmaf_feature_dictionary_set(opts, "clip_db", "true");
    return err;
}

/* Every output of every frame of a run from one extractor; `hip_state` NULL
 * runs the CPU. A missing output is an error, reported by name. */
static int ms_chroma_score(VmafHipState *hip_state, const MsChromaRun *run, MsChromaScores *out)
{
    VmafFeatureDictionary *opts = NULL;
    VmafContext *vmaf = NULL;
    int err = ms_chroma_opts(run, &opts);
    if (err) {
        (void)vmaf_feature_dictionary_free(&opts);
        return err;
    }
    err = ms_context_new(&vmaf, hip_state, opts);
    if (!err)
        err = ms_chroma_feed(vmaf, run);
    const unsigned keys = ms_chroma_key_count(run);
    for (unsigned n = 0; !err && n < keys * MS_CHROMA_FRAMES; n++) {
        err = vmaf_feature_score_at_index(vmaf, ms_chroma_key(run, n % keys),
                                          &out->v[n / keys][n % keys], n / keys);
        if (err) {
            (void)fprintf(stderr, "\n%s %s: no %s at frame %u", hip_state ? "hip" : "cpu",
                          run->fx->what, ms_chroma_key(run, n % keys), n / keys);
        }
    }
    const int closed = vmaf ? vmaf_close(vmaf) : 0;
    return err ? err : closed;
}

/* The outputs of a run that are not the CPU's bit for bit, each reported. */
static unsigned ms_chroma_mismatches(const MsChromaRun *run, const MsChromaScores *cpu,
                                     const MsChromaScores *gpu)
{
    const unsigned keys = ms_chroma_key_count(run);
    unsigned differing = 0u;
    for (unsigned i = 0; i < MS_CHROMA_FRAMES; i++) {
        for (unsigned k = 0; k < keys; k++) {
            if (ms_exact_bits(cpu->v[i][k]) == ms_exact_bits(gpu->v[i][k]))
                continue;
            differing++;
            (void)fprintf(stderr, "\n%s %s frame %u: cpu=%.17g hip=%.17g", run->fx->what,
                          ms_chroma_key(run, k), i, cpu->v[i][k], gpu->v[i][k]);
        }
    }
    return differing;
}

/* 0 when the run matches the CPU (or there is no device), 1 otherwise. A
 * fixture whose chroma scores are 1 compares nothing and fails too. */
static unsigned ms_chroma_run_fails(const MsChromaRun *run)
{
    VmafHipState *hip_state = ms_hip_state();
    if (!hip_state)
        return 0u;
    MsChromaScores cpu;
    MsChromaScores gpu;
    memset(&cpu, 0, sizeof(cpu));
    memset(&gpu, 0, sizeof(gpu));
    const int gpu_err = ms_chroma_score(hip_state, run, &gpu);
    vmaf_hip_state_free(&hip_state);
    if (gpu_err == -ENOSYS) {
        (void)fprintf(stderr, "[skip: integer_ms_ssim_hip is a scaffold (-ENOSYS)] ");
        return 0u;
    }
    const int cpu_err = ms_chroma_score(NULL, run, &cpu);
    const unsigned cb = ms_chroma_key_count(run) - 2u;
    /* Three different scores: neither chroma plane is trivial or luma's. */
    const bool measures =
        run->identical || (cpu.v[0][cb] != cpu.v[0][cb + 1u] && cpu.v[0][cb] != cpu.v[0][0] &&
                           cpu.v[0][cb + 1u] != cpu.v[0][0]);
    if (gpu_err || cpu_err || !measures) {
        (void)fprintf(stderr, "\n%s: hip %d, cpu %d, chroma measured %d", run->fx->what, gpu_err,
                      cpu_err, (int)measures);
        return 1u;
    }
    return ms_chroma_mismatches(run, &cpu, &gpu) ? 1u : 0u;
}

/* Every fixture with enable_lcs and with enable_db, and an identical 4:2:0
 * pair with enable_db and clip_db, where the ceiling binds on every plane. */
static char *test_ms_ssim_chroma_matches_cpu_bit_for_bit(void)
{
    unsigned failing = 0u;
    for (size_t f = 0; f < MS_CHROMA_FIXTURE_COUNT; f++) {
        const MsChromaRun lcs = {.fx = &ms_chroma_fixtures[f], .lcs = true};
        const MsChromaRun db = {.fx = &ms_chroma_fixtures[f], .db = true};
        failing += ms_chroma_run_fails(&lcs) + ms_chroma_run_fails(&db);
    }
    const MsChromaRun ceiling = {
        .fx = &ms_chroma_fixtures[0], .db = true, .clip = true, .identical = true};
    failing += ms_chroma_run_fails(&ceiling);
    mu_assert("float_ms_ssim_hip with enable_chroma is not the CPU extractor's values bit for bit",
              failing == 0u);
    return NULL;
}

/* What one extractor makes of a geometry case: whether the feed succeeds,
 * float_ms_ssim of every frame, and whether float_ms_ssim_cb exists. */
typedef struct MsGeometryVerdict {
    int feed_err;
    double luma[MS_CHROMA_FRAMES];
    bool has_cb;
} MsGeometryVerdict;

static int ms_geometry_verdict(VmafHipState *hip_state, const MsChromaRun *run,
                               MsGeometryVerdict *out)
{
    VmafFeatureDictionary *opts = NULL;
    VmafContext *vmaf = NULL;
    int err = ms_chroma_opts(run, &opts);
    if (err) {
        (void)vmaf_feature_dictionary_free(&opts);
        return err;
    }
    err = ms_context_new(&vmaf, hip_state, opts);
    if (err) {
        if (vmaf)
            (void)vmaf_close(vmaf);
        return err;
    }
    out->feed_err = ms_chroma_feed(vmaf, run);
    for (unsigned i = 0; !out->feed_err && !err && i < MS_CHROMA_FRAMES; i++)
        err = vmaf_feature_score_at_index(vmaf, "float_ms_ssim", &out->luma[i], i);
    double cb = 0.0;
    out->has_cb =
        !out->feed_err && vmaf_feature_score_at_index(vmaf, "float_ms_ssim_cb", &cb, 0u) == 0;
    const int closed = vmaf_close(vmaf);
    return err ? err : closed;
}

/* The same verdict from the CPU and the twin, the luma bits included. */
static bool ms_geometry_verdicts_agree(const MsGeometryVerdict *cpu, const MsGeometryVerdict *gpu)
{
    if ((cpu->feed_err != 0) != (gpu->feed_err != 0) || cpu->has_cb != gpu->has_cb)
        return false;
    for (unsigned i = 0; !cpu->feed_err && i < MS_CHROMA_FRAMES; i++) {
        if (ms_exact_bits(cpu->luma[i]) != ms_exact_bits(gpu->luma[i]))
            return false;
    }
    return true;
}

/* float_ms_ssim.c refuses enable_chroma on a 4:2:0 frame whose chroma is
 * below 176 pixels (256x192 gives 128x96); the twin must too, rather than
 * score luma and drop the chroma outputs. On 4:0:0 the CPU clears the option
 * and scores luma: the twin emits the CPU's float_ms_ssim and no chroma. */
static char *test_ms_ssim_chroma_geometry_verdicts(void)
{
    static const MsChromaFixture small = {"4:2:0 8-bit 256x192", VMAF_PIX_FMT_YUV420P, 256u, 192u,
                                          8u};
    static const MsChromaFixture gray = {"4:0:0 8-bit 256x192", VMAF_PIX_FMT_YUV400P, 256u, 192u,
                                         8u};
    const MsChromaRun refused = {.fx = &small};
    const MsChromaRun luma_only = {.fx = &gray};
    MsGeometryVerdict cpu[2];
    MsGeometryVerdict gpu[2];
    memset(cpu, 0, sizeof(cpu));
    memset(gpu, 0, sizeof(gpu));
    mu_assert("CPU geometry runs failed", ms_geometry_verdict(NULL, &refused, &cpu[0]) == 0 &&
                                              ms_geometry_verdict(NULL, &luma_only, &cpu[1]) == 0);
    mu_assert("CPU: enable_chroma on 256x192 4:2:0 is not refused", cpu[0].feed_err != 0);
    mu_assert("CPU: enable_chroma on 4:0:0 is not luma only", !cpu[1].feed_err && !cpu[1].has_cb);
    VmafHipState *hip_state = ms_hip_state();
    if (!hip_state)
        return NULL;
    const int gpu_err = ms_geometry_verdict(hip_state, &refused, &gpu[0]) |
                        ms_geometry_verdict(hip_state, &luma_only, &gpu[1]);
    vmaf_hip_state_free(&hip_state);
    if (gpu[1].feed_err == -ENOSYS) {
        (void)fprintf(stderr, "[skip: integer_ms_ssim_hip is a scaffold (-ENOSYS)] ");
        return NULL;
    }
    mu_assert("HIP geometry runs failed", gpu_err == 0);
    mu_assert("HIP: enable_chroma on 256x192 4:2:0 is not refused as on the CPU",
              ms_geometry_verdicts_agree(&cpu[0], &gpu[0]));
    mu_assert("HIP: enable_chroma on 4:0:0 is not the CPU's luma-only score",
              ms_geometry_verdicts_agree(&cpu[1], &gpu[1]));
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_ms_ssim_hip_registered);
    mu_run_test(test_ms_ssim_cpu_hip_parity);
    mu_run_test(test_ms_ssim_clip_db_ceiling);
    mu_run_test(test_ms_ssim_matches_cpu_bit_for_bit);
    mu_run_test(test_ms_ssim_frame_sum_order);
    mu_run_test(test_ms_ssim_chroma_matches_cpu_bit_for_bit);
    mu_run_test(test_ms_ssim_chroma_geometry_verdicts);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
