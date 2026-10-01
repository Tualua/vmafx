/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * float_psnr CPU vs. HIP: the twin returns the CPU's score bit for bit
 * (ADR-1440; first added as a places=4 parity test, ADR-0945).
 *
 * float_psnr.c forms each squared difference in float and adds the terms in
 * double, row by row. Every term is a float, so that sum is exact, and a twin
 * returns the same score exactly when its own sum is exact. `float_psnr_hip`
 * added each 16x16 block in fp32. At 8 bits a block of integer squares below
 * 2^16 fits 24 bits and the sum was exact; at 10, 12 and 16 bits the terms
 * are multiples of 1/16, 1/256 and 1/65536, and the block sum rounded as soon
 * as the block's rms difference reached 256 code values. The block sums are
 * integers now, in units of the smallest term.
 *
 * Natural content does not reach that: its differences are small. The
 * fixture here is noise, independent for the reference and the distorted
 * frame and over the full range of the bit depth, so every block's sum of
 * squares is far above 2^24 units. Each case scores two frames at one bit
 * depth and compares with ==. On the fp32 twin the 8-bit case passes and the
 * 10-bit case fails (6e-9 dB at 256x144 and at 960x540); on 576x324 noise it
 * was 2.5e-8 dB off at 12 bits and 1.8e-8 at 16.
 *
 * Skip behaviour: without a HIP device, or on a build without the device
 * kernels (-ENOSYS), a case reports the skip and the run exits 77.
 */

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test.h"

#include "feature/feature_extractor.h"
#include "libvmaf/libvmaf.h"
#include "libvmaf/libvmaf_hip.h"
#include "libvmaf/picture.h"

/* NOLINTBEGIN(modernize-use-nullptr): this is a
 * C23 translation unit, but the required MSVC C lane does not provide the C
 * nullptr spelling clang-tidy proposes. Keep the portable C API form under
 * ADR-1138. */

#ifndef FIXTURE_W
#define FIXTURE_W 256u
#endif
#ifndef FIXTURE_H
#define FIXTURE_H 144u
#endif

#define NUM_FRAMES 2u

/* lowbias32 hash of the position, the frame and the picture: stateless, so
 * both runs see the same pictures. */
static uint32_t sample_hash(unsigned row, unsigned col, unsigned salt)
{
    uint32_t x = ((uint32_t)row << 16) ^ (uint32_t)col ^ (salt * 0x9E3779B9u);
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static void put_sample(VmafPicture *pic, unsigned plane, unsigned row, unsigned col, unsigned v)
{
    uint8_t *line = (uint8_t *)pic->data[plane] + ((size_t)row * (size_t)pic->stride[plane]);
    if (pic->bpc <= 8u) {
        line[col] = (uint8_t)v;
    } else {
        ((uint16_t *)line)[col] = (uint16_t)v;
    }
}

/* Full-range noise on every plane; `salt` separates the frames and the two
 * pictures of a frame. */
static int fill_picture(VmafPicture *pic, unsigned bpc, unsigned salt)
{
    const int err = vmaf_picture_alloc(pic, VMAF_PIX_FMT_YUV420P, bpc, FIXTURE_W, FIXTURE_H);
    if (err) {
        return err;
    }
    for (unsigned p = 0; p < 3u; p++) {
        for (unsigned row = 0; row < pic->h[p]; row++) {
            for (unsigned col = 0; col < pic->w[p]; col++) {
                put_sample(pic, p, row, col, sample_hash(row, col, salt + p) >> (32u - bpc));
            }
        }
    }
    return 0;
}

/* Frame `frame` of the fixture through `vmaf`, which takes both pictures. */
static int feed_frame(VmafContext *vmaf, unsigned bpc, unsigned frame)
{
    VmafPicture ref;
    VmafPicture dist;
    int err = fill_picture(&ref, bpc, (frame * 16u) + 1u);
    if (err) {
        return err;
    }
    err = fill_picture(&dist, bpc, (frame * 16u) + 8u);
    if (err) {
        (void)vmaf_picture_unref(&ref);
        return err;
    }
    return vmaf_read_pictures(vmaf, &ref, &dist, frame);
}

/* NUM_FRAMES frames through the CPU `float_psnr` (`hip_state` NULL) or the
 * twin, and the score of every frame read into `out`. Returns the first
 * error; -ENOSYS is the scaffold build. */
static int float_psnr_scores(VmafHipState *hip_state, unsigned bpc, double *out)
{
    const VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    VmafContext *vmaf = NULL;
    int err = vmaf_init(&vmaf, cfg);
    if (!err && hip_state) {
        err = vmaf_hip_import_state(vmaf, hip_state);
    }
    if (!err) {
        err = vmaf_use_feature(vmaf, hip_state ? "float_psnr_hip" : "float_psnr", NULL);
    }
    for (unsigned frame = 0; frame < NUM_FRAMES && !err; frame++) {
        err = feed_frame(vmaf, bpc, frame);
    }
    if (!err) {
        err = vmaf_read_pictures(vmaf, NULL, NULL, 0);
    }
    for (unsigned frame = 0; frame < NUM_FRAMES && !err; frame++) {
        err = vmaf_feature_score_at_index(vmaf, "float_psnr", &out[frame], frame);
    }
    const int closed = vmaf ? vmaf_close(vmaf) : 0;
    return err ? err : closed;
}

/* The device, or NULL with the reason printed when there is none. */
static VmafHipState *hip_device(void)
{
    VmafHipState *hip_state = NULL;
    const VmafHipConfiguration hip_cfg = {.device_index = -1};
    if (vmaf_hip_state_init(&hip_state, hip_cfg) != 0 || hip_state == NULL) {
        (void)fprintf(stderr, "[skip: no HIP device] ");
        mu_skipped = 1;
        return NULL;
    }
    return hip_state;
}

/* Frames at `bpc` bits whose HIP score is not the CPU's, each one reported;
 * UINT32_MAX when a run failed. A skipped HIP leg counts as 0. */
static unsigned exact_mismatches(unsigned bpc)
{
    double cpu[NUM_FRAMES] = {0.0};
    double gpu[NUM_FRAMES] = {0.0};
    VmafHipState *hip_state = hip_device();
    if (!hip_state) {
        return 0u;
    }
    const int gpu_err = float_psnr_scores(hip_state, bpc, gpu);
    vmaf_hip_state_free(&hip_state);
    if (gpu_err == -ENOSYS) {
        (void)fprintf(stderr, "[skip: HIP kernels not built (enable_hipcc=false)] ");
        mu_skipped = 1;
        return 0u;
    }
    const int cpu_err = gpu_err ? 0 : float_psnr_scores(NULL, bpc, cpu);
    if (gpu_err || cpu_err) {
        (void)fprintf(stderr, "\n%u-bit: run failed (hip %d, cpu %d)\n", bpc, gpu_err, cpu_err);
        return UINT32_MAX;
    }
    unsigned mismatches = 0u;
    for (unsigned frame = 0; frame < NUM_FRAMES; frame++) {
        if (isfinite(cpu[frame]) && cpu[frame] == gpu[frame]) {
            continue;
        }
        mismatches++;
        (void)fprintf(stderr, "\n%ux%u %u-bit frame %u: cpu=%.17g hip=%.17g delta=%.3e\n",
                      FIXTURE_W, FIXTURE_H, bpc, frame, cpu[frame], gpu[frame],
                      fabs(cpu[frame] - gpu[frame]));
    }
    return mismatches;
}

static char *test_float_psnr_hip_registered(void)
{
    VmafFeatureExtractor *fex = vmaf_get_feature_extractor_by_name("float_psnr_hip");
    mu_assert("float_psnr_hip extractor must be registered", fex != NULL);
    mu_assert("float_psnr_hip name matches", !strcmp(fex->name, "float_psnr_hip"));
    return NULL;
}

static char *test_float_psnr_8bit_exact(void)
{
    mu_assert("float_psnr_hip is not bit-identical to the CPU at 8 bits",
              exact_mismatches(8u) == 0u);
    return NULL;
}

static char *test_float_psnr_10bit_exact(void)
{
    mu_assert("float_psnr_hip is not bit-identical to the CPU at 10 bits",
              exact_mismatches(10u) == 0u);
    return NULL;
}

static char *test_float_psnr_12bit_exact(void)
{
    mu_assert("float_psnr_hip is not bit-identical to the CPU at 12 bits",
              exact_mismatches(12u) == 0u);
    return NULL;
}

static char *test_float_psnr_16bit_exact(void)
{
    mu_assert("float_psnr_hip is not bit-identical to the CPU at 16 bits",
              exact_mismatches(16u) == 0u);
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_float_psnr_hip_registered);
    mu_run_test(test_float_psnr_8bit_exact);
    mu_run_test(test_float_psnr_10bit_exact);
    mu_run_test(test_float_psnr_12bit_exact);
    mu_run_test(test_float_psnr_16bit_exact);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
