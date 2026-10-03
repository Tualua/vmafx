/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * Fixed-point ssim CPU vs. GPU twin: the fixtures, the cases and the
 * comparison a twin's parity test wraps (ADR-1443 for SYCL; the fixture and
 * the first eight cases are those of test_cuda_ssim_parity.c, ADR-1424).
 *
 * integer_ssim.c forms each pixel's term in fp64 from int64 window moments
 * and calc_ssim() adds every term into one double in raster order. A twin
 * that computes that term and adds in that order returns the CPU's score bit
 * for bit, so the comparison is equality, not a tolerance.
 *
 * What a case is for is in the comment on it. A twin that forms the term in
 * fp32 differs on every case but the identical frames, the one-pixel frame
 * included; a twin that only adds the terms per block differs on the cases
 * with more than one block.
 *
 * A test describes its backend in one SsimTwin and wraps the ssim_twin_*()
 * cases. Each comparison opens its own device state. Without a device a case
 * is skipped and the test exits 77.
 */

#ifndef LIBVMAF_TEST_SSIM_TWIN_PARITY_H_
#define LIBVMAF_TEST_SSIM_TWIN_PARITY_H_

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test.h"
#include "float_bits.h"

#include "feature/feature_extractor.h"
#include "libvmaf/libvmaf.h"
#include "libvmaf/picture.h"

/* NOLINTBEGIN(modernize-use-nullptr): included by C translation units. The
 * fork builds C as C23, where clang-tidy proposes `nullptr`, but the required
 * MSVC C build does not provide that keyword. Preserve the portable C
 * spelling. ADR-1138. */

#ifndef FIXTURE_W
#define FIXTURE_W 256u
#endif
#ifndef FIXTURE_H
#define FIXTURE_H 144u
#endif

/* One GPU backend's twin, as the shared cases drive it. */
typedef struct SsimTwin {
    const char *extractor; /* registry name, e.g. "integer_ssim_sycl" */
    const char *backend;   /* for messages, e.g. "SYCL" */
    /* Opens a device state. Non-zero: no device, the case is skipped. */
    int (*open)(void **state);
    int (*import)(VmafContext *vmaf, void *state);
    int (*close)(void *state);
} SsimTwin;

/* How the distorted frame relates to the reference. */
enum SsimTwinDistortion {
    SSIM_TWIN_NOISE,    /* the reference plus a position-dependent error */
    SSIM_TWIN_INVERTED, /* peak minus the reference: negative covariances */
    SSIM_TWIN_SAME,     /* the reference itself */
};

typedef struct SsimTwinCase {
    const char *what;
    unsigned w;
    unsigned h;
    unsigned bpc;
    enum SsimTwinDistortion distortion;
    const char *option;  /* NULL for the defaults */
    const char *option2; /* a second boolean option, or NULL */
} SsimTwinCase;

/* Deterministic position hash. */
static inline unsigned ssim_twin_hash(unsigned row, unsigned col, unsigned salt)
{
    unsigned x = row * 73856093u ^ col * 19349663u ^ salt * 83492791u;
    x ^= x >> 13;
    x *= 0x5bd1e995u;
    x ^= x >> 15;
    return x;
}

/* Luma in 8-bit levels: structure that varies from window to window, so the
 * per-pixel terms span a range of magnitudes and their sum depends on its
 * order. */
static inline unsigned ssim_twin_luma(unsigned row, unsigned col,
                                      enum SsimTwinDistortion distortion, bool distorted)
{
    const unsigned base = 48u + (ssim_twin_hash(row >> 2, col >> 2, 1u) % 160u);
    if (!distorted || distortion == SSIM_TWIN_SAME)
        return base;
    if (distortion == SSIM_TWIN_INVERTED)
        return 255u - base;
    return base + (ssim_twin_hash(row, col, 2u) % 23u);
}

static inline void ssim_twin_put_sample(VmafPicture *pic, unsigned plane, unsigned row,
                                        unsigned col, unsigned v)
{
    const unsigned peak = (1u << pic->bpc) - 1u;
    uint8_t *line = (uint8_t *)pic->data[plane] + (size_t)row * (size_t)pic->stride[plane];
    if (pic->bpc <= 8u) {
        line[col] = (uint8_t)(v > peak ? peak : v);
    } else {
        ((uint16_t *)line)[col] = (uint16_t)(v > peak ? peak : v);
    }
}

static inline int ssim_twin_fill_picture(VmafPicture *pic, const SsimTwinCase *c, bool distorted)
{
    const int err = vmaf_picture_alloc(pic, VMAF_PIX_FMT_YUV420P, c->bpc, c->w, c->h);
    if (err)
        return err;
    const unsigned gain = 1u << (c->bpc - 8u);
    for (unsigned row = 0; row < pic->h[0]; row++) {
        for (unsigned col = 0; col < pic->w[0]; col++) {
            ssim_twin_put_sample(pic, 0u, row, col,
                                 ssim_twin_luma(row, col, c->distortion, distorted) * gain);
        }
    }
    for (unsigned p = 1; p < 3; p++) {
        for (unsigned row = 0; row < pic->h[p]; row++) {
            for (unsigned col = 0; col < pic->w[p]; col++)
                ssim_twin_put_sample(pic, p, row, col, 128u * gain);
        }
    }
    return 0;
}

static inline int ssim_twin_options(const SsimTwinCase *c, VmafFeatureDictionary **dict)
{
    *dict = NULL;
    if (c->option && vmaf_feature_dictionary_set(dict, c->option, "true"))
        return -1;
    if (c->option2 && vmaf_feature_dictionary_set(dict, c->option2, "true"))
        return -1;
    return 0;
}

/* Feed the case's frame, flush, and read the score. */
static inline mu_message_t ssim_twin_read(VmafContext *vmaf, const SsimTwinCase *c, double *score)
{
    VmafPicture ref;
    VmafPicture dist;
    mu_assert("fill reference failed", !ssim_twin_fill_picture(&ref, c, false));
    mu_assert("fill distorted failed", !ssim_twin_fill_picture(&dist, c, true));
    mu_assert("vmaf_read_pictures failed", !vmaf_read_pictures(vmaf, &ref, &dist, 0u));
    mu_assert("vmaf_read_pictures(EOS) failed", !vmaf_read_pictures(vmaf, NULL, NULL, 0));
    mu_assert("vmaf_feature_score_at_index(ssim) failed",
              !vmaf_feature_score_at_index(vmaf, "ssim", score, 0u));
    return NULL;
}

/* The case's score on the CPU (`state` NULL) or on the twin. */
static inline mu_message_t ssim_twin_score(const SsimTwin *twin, void *state, const SsimTwinCase *c,
                                           double *score)
{
    VmafContext *vmaf = NULL;
    VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    mu_assert("vmaf_init failed", !vmaf_init(&vmaf, cfg));
    mu_message_t msg = NULL;
    VmafFeatureDictionary *options = NULL;
    if (state && twin->import(vmaf, state))
        msg = "importing the device state failed";
    if (!msg && ssim_twin_options(c, &options))
        msg = "setting the case's option failed";
    if (!msg && vmaf_use_feature(vmaf, state ? twin->extractor : "ssim", options))
        msg = "vmaf_use_feature failed";
    if (!msg)
        msg = ssim_twin_read(vmaf, c, score);
    if (vmaf_close(vmaf) != 0 && !msg)
        msg = "vmaf_close failed";
    return msg;
}

/* The twin's score equals the CPU's, bit for bit (two infinities of one sign
 * are equal). A missing device skips the case. */
static inline mu_message_t ssim_twin_check(const SsimTwin *twin, const SsimTwinCase *c)
{
    double cpu = 0.0;
    double gpu = NAN;
    mu_message_t msg = ssim_twin_score(twin, NULL, c, &cpu);
    if (msg)
        return msg;
    void *state = NULL;
    if (twin->open(&state) != 0 || state == NULL) {
        (void)fprintf(stderr, "[skip: no %s device] ", twin->backend);
        mu_skipped = 1;
        return NULL;
    }
    msg = ssim_twin_score(twin, state, c, &gpu);
    const int close_err = twin->close(state);
    if (msg)
        return msg;
    mu_assert("closing the device state failed", close_err == 0);
    mu_assert("CPU ssim is NaN", !isnan(cpu));
    const bool identical = vmaf_test_identical_f64(cpu, gpu);
    if (!identical) {
        (void)fprintf(stderr, "\n%s %ux%u %u-bit: cpu=%.17g %s=%.17g delta=%.3e\n", c->what, c->w,
                      c->h, c->bpc, cpu, twin->backend, gpu, fabs(cpu - gpu));
    }
    mu_assert("the ssim twin differs from the CPU extractor", identical);
    return NULL;
}

static inline mu_message_t ssim_twin_registered(const SsimTwin *twin)
{
    VmafFeatureExtractor *fex = vmaf_get_feature_extractor_by_name(twin->extractor);
    mu_assert("the ssim twin must be registered", fex != NULL);
    mu_assert("the ssim twin's name matches", !strcmp(fex->name, twin->extractor));
    return NULL;
}

/* The build's fixture at one bit depth. At 8 and 10 bits every product of
 * two moments is below 2^52 and the reference's integer sums are exact; at
 * 12 bits the fixture's dark windows are below that bound and its bright ones
 * above it; at 16 bits samplemax^2 exceeds INT_MAX and every product rounds. */
static inline mu_message_t ssim_twin_bit_depth(const SsimTwin *twin, unsigned bpc)
{
    const SsimTwinCase c = {"ssim", FIXTURE_W, FIXTURE_H, bpc, SSIM_TWIN_NOISE, NULL, NULL};
    return ssim_twin_check(twin, &c);
}

/* Neither dimension is a multiple of 16 or 8, the block a twin may reduce
 * over. */
static inline mu_message_t ssim_twin_odd_frame(const SsimTwin *twin)
{
    const SsimTwinCase c = {"ssim odd", 323u, 181u, 8u, SSIM_TWIN_NOISE, NULL, NULL};
    return ssim_twin_check(twin, &c);
}

/* Narrower than the nine-tap window: every pixel's window is truncated on
 * both sides and its weight is not a power of two. */
static inline mu_message_t ssim_twin_tiny_frame(const SsimTwin *twin, unsigned bpc)
{
    const SsimTwinCase c = {"ssim tiny", 7u, 5u, bpc, SSIM_TWIN_NOISE, NULL, NULL};
    return ssim_twin_check(twin, &c);
}

/* One pixel: one window of one tap each way. */
static inline mu_message_t ssim_twin_one_pixel(const SsimTwin *twin)
{
    const SsimTwinCase c = {"ssim 1x1", 1u, 1u, 8u, SSIM_TWIN_NOISE, NULL, NULL};
    return ssim_twin_check(twin, &c);
}

static inline mu_message_t ssim_twin_1080p(const SsimTwin *twin)
{
    const SsimTwinCase c = {"ssim 1080p", 1920u, 1080u, 8u, SSIM_TWIN_NOISE, NULL, NULL};
    return ssim_twin_check(twin, &c);
}

/* -10 * log10(1 - ssim) magnifies a last-place difference of the ratio. */
static inline mu_message_t ssim_twin_enable_db(const SsimTwin *twin)
{
    const SsimTwinCase c = {"ssim enable_db", FIXTURE_W,   FIXTURE_H, 8u,
                            SSIM_TWIN_NOISE,  "enable_db", NULL};
    return ssim_twin_check(twin, &c);
}

/* The distorted frame is the reference's negative: the covariance of every
 * textured window is negative, so terms below zero enter the sum. */
static inline mu_message_t ssim_twin_inverted(const SsimTwin *twin, unsigned bpc)
{
    const SsimTwinCase c = {"ssim inverted",    FIXTURE_W, FIXTURE_H, bpc,
                            SSIM_TWIN_INVERTED, NULL,      NULL};
    return ssim_twin_check(twin, &c);
}

/* Identical frames in dB. The CPU's score is 1 (+inf dB, or the ceiling with
 * clip_db) on most frames and one unit in the last place below 1 on some
 * (159.55 dB): which it is depends on every term and on the order of the
 * sum. */
static inline mu_message_t ssim_twin_identical(const SsimTwin *twin, unsigned w, unsigned h,
                                               const char *option2)
{
    const SsimTwinCase c = {"ssim identical", w, h, 8u, SSIM_TWIN_SAME, "enable_db", option2};
    return ssim_twin_check(twin, &c);
}

/* NOLINTEND(modernize-use-nullptr) */

#endif /* LIBVMAF_TEST_SSIM_TWIN_PARITY_H_ */
