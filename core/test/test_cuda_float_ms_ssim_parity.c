/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * ADR-0947 — float_ms_ssim CPU vs. CUDA parity test (round 3).
 *
 * The float-path multi-scale SSIM extractor is implemented
 * independently in core/src/feature/float_ms_ssim.c (CPU) and
 * core/src/feature/cuda/integer_ms_ssim_cuda.c (CUDA, registered as
 * `float_ms_ssim_cuda`).  Both emit the scalar `float_ms_ssim`
 * feature.  Before this test no cross-backend assertion gated drift;
 * any kernel-grid or SIMD pivot could silently shift the score.
 *
 * The 5-scale MS-SSIM pyramid requires a fixture wide enough that the
 * smallest scale (/16) still has non-trivial dimensions.  256x144
 * gives 16x9 at scale 4 — well above the 11x11 Gaussian window.
 *
 * Asserts agreement to within 1e-4 (places=4, ADR-0214) at frame
 * index 1 across 3 frames, and since ADR-1403 that every output of
 * every frame, the 15 per-scale l / c / s means included, is the
 * CPU's value bit for bit.  With enable_chroma, float_ms_ssim_cb and
 * float_ms_ssim_cr are compared the same way on 4:2:0, 4:2:2 and 4:4:4
 * frames, and the twin must refuse what the CPU refuses
 * (T-MS-SSIM-GPU-CHROMA-OPTION-DRIFT-2026-09-06).  Skips cleanly when no
 * CUDA device is visible.
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test.h"

#include "libvmaf/libvmaf.h"
#include "libvmaf/libvmaf_cuda.h"
#include "libvmaf/picture.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy also proposes the `nullptr` keyword, but MSVC's
 * documented /std:clatest C23 feature set does not include `nullptr` while the
 * required Windows build compiles this TU with cl.exe, and this test mirrors
 * the C spelling of the surface it exercises. ADR-1138. */

/* The 5-level 11-tap MS-SSIM pyramid requires min(w,h) >= 11<<4 = 176.
 * 256x192 keeps both axes above the floor with a clean 16-px multiple. */
#ifndef FIXTURE_W
#define FIXTURE_W 256u
#endif
#ifndef FIXTURE_H
#define FIXTURE_H 192u
#endif
#define FIXTURE_BPC 8u
#define NUM_FRAMES 3u

#define PARITY_TOL 1e-4

static int fill_ref(VmafPicture *pic, unsigned frame_idx)
{
    int err = vmaf_picture_alloc(pic, VMAF_PIX_FMT_YUV420P, FIXTURE_BPC, FIXTURE_W, FIXTURE_H);
    if (err)
        return err;

    uint8_t *y = (uint8_t *)pic->data[0];
    for (unsigned row = 0; row < pic->h[0]; row++) {
        for (unsigned col = 0; col < pic->w[0]; col++) {
            y[row * pic->stride[0] + col] = (uint8_t)((row + col + frame_idx * 7u) & 0xFFu);
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

static int fill_dist(VmafPicture *pic, unsigned frame_idx)
{
    int err = vmaf_picture_alloc(pic, VMAF_PIX_FMT_YUV420P, FIXTURE_BPC, FIXTURE_W, FIXTURE_H);
    if (err)
        return err;

    uint8_t *y = (uint8_t *)pic->data[0];
    for (unsigned row = 0; row < pic->h[0]; row++) {
        for (unsigned col = 0; col < pic->w[0]; col++) {
            const unsigned base = (row + col + frame_idx * 7u) & 0xFFu;
            const unsigned noise = ((row * 2u + col + frame_idx * 3u) % 9u);
            y[row * pic->stride[0] + col] = (uint8_t)((base + noise) & 0xFFu);
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

static char *feed_ms_ssim_frames(VmafContext *vmaf, bool identical)
{
    for (unsigned i = 0; i < NUM_FRAMES; i++) {
        VmafPicture ref;
        VmafPicture dist;
        int err = fill_ref(&ref, i);
        mu_assert("fill_ref failed", !err);
        err = identical ? fill_ref(&dist, i) : fill_dist(&dist, i);
        mu_assert("fill_dist failed", !err);
        err = vmaf_read_pictures(vmaf, &ref, &dist, i);
        mu_assert("vmaf_read_pictures failed", !err);
    }
    int err = vmaf_read_pictures(vmaf, NULL, NULL, 0);
    mu_assert("vmaf_read_pictures(EOS) failed", !err);
    return NULL;
}

static char *run_cpu(bool db, bool identical, double *out_score)
{
    VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    VmafContext *vmaf = NULL;
    int err = vmaf_init(&vmaf, cfg);
    mu_assert("CPU: vmaf_init failed", !err);

    VmafFeatureDictionary *opts = NULL;
    if (db) {
        err = ms_ssim_db_opts(&opts);
        mu_assert("CPU: ms_ssim_db_opts failed", !err);
    }
    /* vmaf_use_feature() consumes the dictionary, on failure too. */
    err = vmaf_use_feature(vmaf, "float_ms_ssim", opts);
    mu_assert("CPU: vmaf_use_feature(float_ms_ssim) failed", !err);

    mu_assert_msg(feed_ms_ssim_frames(vmaf, identical));

    err = vmaf_feature_score_at_index(vmaf, "float_ms_ssim", out_score, 1u);
    mu_assert("CPU: vmaf_feature_score_at_index(float_ms_ssim, idx=1) failed", !err);

    err = vmaf_close(vmaf);
    mu_assert("CPU: vmaf_close failed", !err);
    return NULL;
}

static char *setup_cuda_ms_ssim_context(VmafContext **out_vmaf, VmafCudaState **out_cu_state,
                                        bool db)
{
    *out_vmaf = NULL;
    *out_cu_state = NULL;

    VmafCudaState *cu_state = NULL;
    VmafCudaConfiguration cuda_cfg = {0};
    int err = vmaf_cuda_state_init(&cu_state, cuda_cfg);
    if (err != 0 || cu_state == NULL) {
        (void)fprintf(stderr, "[skip: no CUDA device] ");
        return NULL;
    }

    VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    VmafContext *vmaf = NULL;
    err = vmaf_init(&vmaf, cfg);
    mu_assert("CUDA: vmaf_init failed", !err);

    err = vmaf_cuda_import_state(vmaf, cu_state);
    mu_assert("CUDA: vmaf_cuda_import_state failed", !err);

    VmafFeatureDictionary *opts = NULL;
    if (db) {
        err = ms_ssim_db_opts(&opts);
        mu_assert("CUDA: ms_ssim_db_opts failed", !err);
    }
    /* vmaf_use_feature() consumes the dictionary, on failure too. */
    err = vmaf_use_feature(vmaf, "float_ms_ssim_cuda", opts);
    mu_assert("CUDA: vmaf_use_feature(float_ms_ssim_cuda) failed", !err);

    *out_vmaf = vmaf;
    *out_cu_state = cu_state;
    return NULL;
}

static char *run_cuda(bool db, bool identical, double *out_score)
{
    *out_score = NAN;

    VmafContext *vmaf = NULL;
    VmafCudaState *cu_state = NULL;
    mu_assert_msg(setup_cuda_ms_ssim_context(&vmaf, &cu_state, db));
    if (!vmaf)
        return NULL;

    mu_assert_msg(feed_ms_ssim_frames(vmaf, identical));

    int err = vmaf_feature_score_at_index(vmaf, "float_ms_ssim", out_score, 1u);
    mu_assert("CUDA: vmaf_feature_score_at_index(float_ms_ssim, idx=1) failed", !err);

    err = vmaf_close(vmaf);
    mu_assert("CUDA: vmaf_close failed", !err);
    err = vmaf_cuda_state_free(cu_state);
    mu_assert("CUDA: vmaf_cuda_state_free failed", !err);
    return NULL;
}

static char *test_float_ms_ssim_cpu_cuda_parity(void)
{
    double cpu_score = 0.0;
    double cuda_score = NAN;

    char *msg = run_cpu(false, false, &cpu_score);
    if (msg)
        return msg;
    msg = run_cuda(false, false, &cuda_score);
    if (msg)
        return msg;
    if (isnan(cuda_score))
        return NULL;

    mu_assert("CPU float_ms_ssim score is non-finite", isfinite(cpu_score));
    mu_assert("CUDA float_ms_ssim score is non-finite", isfinite(cuda_score));

    double delta = fabs(cpu_score - cuda_score);
    if (delta > PARITY_TOL) {
        (void)fprintf(stderr,
                      "\nfloat_ms_ssim parity FAIL: cpu=%.8f cuda=%.8f delta=%.2e tol=%.2e\n",
                      cpu_score, cuda_score, delta, PARITY_TOL);
    }
    mu_assert("float_ms_ssim CPU vs. CUDA delta exceeds places=4 tolerance (1e-4)",
              delta <= PARITY_TOL);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* ADR-1221 — clip_db is a CEILING on the dB output, not a clamp on the */
/* linear score.                                                        */
/*                                                                     */
/* float_ms_ssim.c derives `max_db = ceil(10*log10(peak*peak/mse))`     */
/* with `mse = 0.5/(w*h)` and returns                                   */
/* `MIN(-10*log10(1 - score), max_db)`, short-circuiting to `max_db`    */
/* when score >= 1.0. The twin used to clamp the LINEAR score into      */
/* [0, 1] and then convert with no ceiling, which returns +Inf for an   */
/* identical reference/distorted pair.                                  */
/*                                                                     */
/* The fixture feeds the SAME picture as reference and distorted, so    */
/* ms_ssim is 1.0 and the ceiling actually binds. On a merely           */
/* high-similarity pair `-10*log10(1 - score)` stays well below max_db  */
/* and both paths agree, which is why this needs its own fixture rather */
/* than the shared one. Scoring a file against itself is an ordinary    */
/* thing to do, so this is a reachable case, not a synthetic one.       */
/* ------------------------------------------------------------------ */
static char *test_float_ms_ssim_clip_db_ceiling(void)
{
    double cpu_score = 0.0;
    double gpu_score = NAN;

    char *msg = run_cpu(true, true, &cpu_score);
    if (msg)
        return msg;
    msg = run_cuda(true, true, &gpu_score);
    if (msg)
        return msg;
    if (isnan(gpu_score))
        return NULL;

    mu_assert("CPU float_ms_ssim dB score is non-finite", isfinite(cpu_score));
    mu_assert("CUDA float_ms_ssim dB score is non-finite -- clip_db must cap it at max_db",
              isfinite(gpu_score));

    const double delta = fabs(cpu_score - gpu_score);
    if (delta > PARITY_TOL) {
        (void)fprintf(stderr,
                      "\nfloat_ms_ssim enable_db+clip_db parity FAIL: cpu=%.8f cuda=%.8f "
                      "delta=%.2e tol=%.2e\n",
                      cpu_score, gpu_score, delta, PARITY_TOL);
    }
    mu_assert("float_ms_ssim dB score drifts from the CPU reference", delta <= PARITY_TOL);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* ADR-1403 — float_ms_ssim_cuda is the CPU's arithmetic, bit for bit.   */
/*                                                                     */
/* The kernels reproduce ms_ssim_decimate.c (one fused multiply-add per */
/* tap), iqa_convolve() (fp32 products summed in fp64) and              */
/* ssim_accumulate_default_scalar() (fp32 denominators and quotient)    */
/* operation for operation, the fatbin builds without FMA contraction,  */
/* and the host rounds each per-scale mean to fp32 and combines as      */
/* ms_ssim.c does. Before that the twin ran fp32 window sums, fp64      */
/* denominators and unrounded means, and was 2.4e-8 to 3.8e-6 from the  */
/* CPU on the Netflix pair, the checkerboards and BBB 4K; turning       */
/* contraction off alone made it worse at 4K.                           */
/*                                                                     */
/* With enable_lcs the 15 per-scale l / c / s means are compared too:   */
/* every one of the 16 outputs of every frame must equal the CPU's.     */
/* ------------------------------------------------------------------ */
#define MS_EXACT_KEYS 16u

typedef struct MsExactScores {
    double v[NUM_FRAMES][MS_EXACT_KEYS];
} MsExactScores;

static const char *const ms_exact_keys[MS_EXACT_KEYS] = {
    "float_ms_ssim",          "float_ms_ssim_l_scale0", "float_ms_ssim_l_scale1",
    "float_ms_ssim_l_scale2", "float_ms_ssim_l_scale3", "float_ms_ssim_l_scale4",
    "float_ms_ssim_c_scale0", "float_ms_ssim_c_scale1", "float_ms_ssim_c_scale2",
    "float_ms_ssim_c_scale3", "float_ms_ssim_c_scale4", "float_ms_ssim_s_scale0",
    "float_ms_ssim_s_scale1", "float_ms_ssim_s_scale2", "float_ms_ssim_s_scale3",
    "float_ms_ssim_s_scale4",
};

static int ms_exact_feed(VmafContext *vmaf)
{
    for (unsigned i = 0; i < NUM_FRAMES; i++) {
        VmafPicture ref;
        VmafPicture dist;
        int err = fill_ref(&ref, i);
        if (err)
            return err;
        err = fill_dist(&dist, i);
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

static int ms_exact_collect(VmafContext *vmaf, MsExactScores *out)
{
    for (unsigned k = 0; k < MS_EXACT_KEYS; k++) {
        for (unsigned i = 0; i < NUM_FRAMES; i++) {
            const int err = vmaf_feature_score_at_index(vmaf, ms_exact_keys[k], &out->v[i][k], i);
            if (err)
                return err;
        }
    }
    return 0;
}

/* Every frame's 16 outputs from one extractor; `cu_state` NULL runs the CPU. */
static int ms_exact_score(VmafCudaState *cu_state, MsExactScores *out)
{
    VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    VmafContext *vmaf = NULL;
    int err = vmaf_init(&vmaf, cfg);
    if (err)
        return err;
    if (cu_state)
        err = vmaf_cuda_import_state(vmaf, cu_state);
    VmafFeatureDictionary *opts = NULL;
    if (!err)
        err = vmaf_feature_dictionary_set(&opts, "enable_lcs", "true");
    if (!err) {
        err = vmaf_use_feature(vmaf, cu_state ? "float_ms_ssim_cuda" : "float_ms_ssim", opts);
        opts = NULL; /* consumed on every path (libvmaf.h) */
    }
    if (opts)
        (void)vmaf_feature_dictionary_free(&opts);
    if (!err)
        err = ms_exact_feed(vmaf);
    if (!err)
        err = ms_exact_collect(vmaf, out);
    const int closed = vmaf_close(vmaf);
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
    for (unsigned i = 0; i < NUM_FRAMES; i++) {
        for (unsigned k = 0; k < MS_EXACT_KEYS; k++) {
            if (ms_exact_bits(cpu->v[i][k]) == ms_exact_bits(gpu->v[i][k]))
                continue;
            differing++;
            (void)fprintf(stderr, "\n%s frame %u: cpu=%.17g cuda=%.17g delta=%.3e",
                          ms_exact_keys[k], i, cpu->v[i][k], gpu->v[i][k],
                          fabs(cpu->v[i][k] - gpu->v[i][k]));
        }
    }
    return differing;
}

static char *test_float_ms_ssim_matches_cpu_bit_for_bit(void)
{
    VmafCudaState *cu_state = NULL;
    VmafCudaConfiguration cuda_cfg = {0};
    if (vmaf_cuda_state_init(&cu_state, cuda_cfg) != 0 || !cu_state) {
        (void)fprintf(stderr, "[skip: no CUDA device] ");
        return NULL;
    }
    MsExactScores cpu;
    MsExactScores gpu;
    memset(&cpu, 0, sizeof(cpu));
    memset(&gpu, 0, sizeof(gpu));
    const int gpu_err = ms_exact_score(cu_state, &gpu);
    const int freed = vmaf_cuda_state_free(cu_state);
    mu_assert("CUDA: float_ms_ssim_cuda with enable_lcs failed", gpu_err == 0 && freed == 0);
    mu_assert("CPU: float_ms_ssim with enable_lcs failed", ms_exact_score(NULL, &cpu) == 0);
    mu_assert("CPU float_ms_ssim is not a usable reference",
              isfinite(cpu.v[1][0]) && cpu.v[1][0] > 0.0 && cpu.v[1][0] < 1.0);
    mu_assert("float_ms_ssim_cuda is not the CPU extractor's value bit for bit",
              ms_exact_mismatches(&cpu, &gpu) == 0u);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* T-MS-SSIM-GPU-CHROMA-OPTION-DRIFT-2026-09-06 — enable_chroma.         */
/*                                                                     */
/* float_ms_ssim.c runs its pipeline once per plane and emits           */
/* float_ms_ssim_cb and float_ms_ssim_cr. Until 2026-10-03 this twin    */
/* had no enable_chroma option: vmaf_use_feature() refused it, and the  */
/* CLI kept the CPU extractor for such a request. These cases fail on   */
/* that code. Each fixture has its own texture and distortion per       */
/* plane, so a twin that scored one plane three times, or a chroma      */
/* plane with luma's row pitch, would differ. The 4:2:0 frame has odd   */
/* dimensions (ceil-subsampled 177x178 chroma), the 4:2:2 one is 10-bit */
/* and the 4:4:4 one has full-size chroma.                              */
/* ------------------------------------------------------------------ */
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
    double v[NUM_FRAMES][MS_CHROMA_KEYS];
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
    for (unsigned i = 0; i < NUM_FRAMES; i++) {
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

/* A context with the run's options on `float_ms_ssim_cuda` over `cu_state`,
 * or on the CPU's `float_ms_ssim` when it is NULL. */
static int ms_chroma_context(VmafContext **vmaf, VmafCudaState *cu_state, const MsChromaRun *run)
{
    const VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    VmafFeatureDictionary *opts = NULL;
    int err = vmaf_init(vmaf, cfg);
    if (!err && cu_state)
        err = vmaf_cuda_import_state(*vmaf, cu_state);
    if (!err)
        err = ms_chroma_opts(run, &opts);
    if (!err) {
        err = vmaf_use_feature(*vmaf, cu_state ? "float_ms_ssim_cuda" : "float_ms_ssim", opts);
        opts = NULL; /* consumed on every path (libvmaf.h) */
    }
    if (opts)
        (void)vmaf_feature_dictionary_free(&opts);
    return err;
}

/* Every output of every frame of a run from one extractor; `cu_state` NULL
 * runs the CPU. A missing output is an error, reported by name. */
static int ms_chroma_score(VmafCudaState *cu_state, const MsChromaRun *run, MsChromaScores *out)
{
    VmafContext *vmaf = NULL;
    int err = ms_chroma_context(&vmaf, cu_state, run);
    if (!err)
        err = ms_chroma_feed(vmaf, run);
    const unsigned keys = ms_chroma_key_count(run);
    for (unsigned n = 0; !err && n < keys * NUM_FRAMES; n++) {
        err = vmaf_feature_score_at_index(vmaf, ms_chroma_key(run, n % keys),
                                          &out->v[n / keys][n % keys], n / keys);
        if (err) {
            (void)fprintf(stderr, "\n%s %s: no %s at frame %u", cu_state ? "cuda" : "cpu",
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
    for (unsigned i = 0; i < NUM_FRAMES; i++) {
        for (unsigned k = 0; k < keys; k++) {
            if (ms_exact_bits(cpu->v[i][k]) == ms_exact_bits(gpu->v[i][k]))
                continue;
            differing++;
            (void)fprintf(stderr, "\n%s %s frame %u: cpu=%.17g cuda=%.17g", run->fx->what,
                          ms_chroma_key(run, k), i, cpu->v[i][k], gpu->v[i][k]);
        }
    }
    return differing;
}

/* The device, or NULL with the reason printed when there is none. */
static VmafCudaState *ms_cuda_state(void)
{
    VmafCudaState *cu_state = NULL;
    VmafCudaConfiguration cuda_cfg = {0};
    if (vmaf_cuda_state_init(&cu_state, cuda_cfg) != 0 || !cu_state) {
        (void)fprintf(stderr, "[skip: no CUDA device] ");
        return NULL;
    }
    return cu_state;
}

/* 0 when the run matches the CPU (or there is no device), 1 otherwise. A
 * fixture whose chroma scores are trivial compares nothing and fails too. */
static unsigned ms_chroma_run_fails(const MsChromaRun *run)
{
    VmafCudaState *cu_state = ms_cuda_state();
    if (!cu_state)
        return 0u;
    MsChromaScores cpu;
    MsChromaScores gpu;
    memset(&cpu, 0, sizeof(cpu));
    memset(&gpu, 0, sizeof(gpu));
    const int gpu_err = ms_chroma_score(cu_state, run, &gpu);
    const int freed = vmaf_cuda_state_free(cu_state);
    const int cpu_err = ms_chroma_score(NULL, run, &cpu);
    const unsigned cb = ms_chroma_key_count(run) - 2u;
    /* Three different scores: neither chroma plane is trivial or luma's. */
    const bool measures =
        run->identical || (cpu.v[0][cb] != cpu.v[0][cb + 1u] && cpu.v[0][cb] != cpu.v[0][0] &&
                           cpu.v[0][cb + 1u] != cpu.v[0][0]);
    if (gpu_err || freed || cpu_err || !measures) {
        (void)fprintf(stderr, "\n%s: cuda %d, cpu %d, chroma measured %d", run->fx->what, gpu_err,
                      cpu_err, (int)measures);
        return 1u;
    }
    return ms_chroma_mismatches(run, &cpu, &gpu) ? 1u : 0u;
}

/* Every fixture with enable_lcs and with enable_db, and an identical 4:2:0
 * pair with enable_db and clip_db, where the ceiling binds on every plane. */
static char *test_float_ms_ssim_chroma_matches_cpu_bit_for_bit(void)
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
    mu_assert("float_ms_ssim_cuda with enable_chroma is not the CPU extractor's values bit for bit",
              failing == 0u);
    return NULL;
}

/* What one extractor makes of a geometry case: whether the feed succeeds,
 * float_ms_ssim of every frame, and whether float_ms_ssim_cb exists. */
typedef struct MsGeometryVerdict {
    int feed_err;
    double luma[NUM_FRAMES];
    bool has_cb;
} MsGeometryVerdict;

static int ms_geometry_verdict(VmafCudaState *cu_state, const MsChromaRun *run,
                               MsGeometryVerdict *out)
{
    VmafContext *vmaf = NULL;
    int err = ms_chroma_context(&vmaf, cu_state, run);
    if (err) {
        if (vmaf)
            (void)vmaf_close(vmaf);
        return err;
    }
    out->feed_err = ms_chroma_feed(vmaf, run);
    for (unsigned i = 0; !out->feed_err && !err && i < NUM_FRAMES; i++)
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
    for (unsigned i = 0; !cpu->feed_err && i < NUM_FRAMES; i++) {
        if (ms_exact_bits(cpu->luma[i]) != ms_exact_bits(gpu->luma[i]))
            return false;
    }
    return true;
}

/* The twin's verdict on its own device state: a state is imported into one
 * context only. Sets `*skipped` when there is no device. */
static int ms_geometry_on_device(const MsChromaRun *run, MsGeometryVerdict *out, bool *skipped)
{
    VmafCudaState *cu_state = ms_cuda_state();
    *skipped = cu_state == NULL;
    if (*skipped)
        return 0;
    const int err = ms_geometry_verdict(cu_state, run, out);
    const int freed = vmaf_cuda_state_free(cu_state);
    if (err || freed) {
        (void)fprintf(stderr, "\ncuda %s: run %d (feed %d), free %d", run->fx->what, err,
                      out->feed_err, freed);
    }
    return err ? err : freed;
}

/* float_ms_ssim.c refuses enable_chroma on a 4:2:0 frame whose chroma is
 * below 176 pixels (256x192 gives 128x96); the twin must too. On 4:0:0 the
 * CPU clears the option and scores luma: the twin emits the CPU's
 * float_ms_ssim and no chroma. */
static char *test_float_ms_ssim_chroma_geometry_verdicts(void)
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
    bool skipped = false;
    const int refused_err = ms_geometry_on_device(&refused, &gpu[0], &skipped);
    if (skipped)
        return NULL;
    const int gray_err = ms_geometry_on_device(&luma_only, &gpu[1], &skipped);
    mu_assert("CUDA geometry runs failed", !refused_err && !gray_err);
    mu_assert("CUDA: enable_chroma on 256x192 4:2:0 is not refused as on the CPU",
              ms_geometry_verdicts_agree(&cpu[0], &gpu[0]));
    mu_assert("CUDA: enable_chroma on 4:0:0 is not the CPU's luma-only score",
              ms_geometry_verdicts_agree(&cpu[1], &gpu[1]));
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_float_ms_ssim_cpu_cuda_parity);
    mu_run_test(test_float_ms_ssim_clip_db_ceiling);
    mu_run_test(test_float_ms_ssim_matches_cpu_bit_for_bit);
    mu_run_test(test_float_ms_ssim_chroma_matches_cpu_bit_for_bit);
    mu_run_test(test_float_ms_ssim_chroma_geometry_verdicts);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
