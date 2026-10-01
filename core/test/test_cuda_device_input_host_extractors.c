/**
 * Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * A CPU extractor scores device-resident input on all three planes
 * (Netflix/vmaf#1613, T-CUDA-DEVICE-INPUT-CHROMA-NOT-DOWNLOADED-2026-10-01).
 *
 * With the pictures in device memory (the FFmpeg `libvmaf_cuda` path hands
 * libvmaf such pictures) and a CPU extractor registered, vmaf_read_pictures()
 * downloads the device pictures into host pictures for it. It copied the luma
 * plane only, so the chroma planes of the host pictures were whatever
 * vmaf_picture_alloc() left there: `psnr_cb` and `psnr_cr` came out as the
 * 60 dB cap or as garbage, and no error was reported. Here `gpumask = 1` keeps
 * the CUDA twins off, so `psnr` runs on the CPU while the pictures come from
 * the device pool; every plane of every frame is compared with a CPU-only run
 * on the same pictures, with `n_threads` 0 and 4.
 *
 * The test skips (exit 77) when no CUDA device is visible.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mu_table.h"
#include "test.h"

#include "cuda/common.h"
#include "cuda/picture_cuda.h"
#include "libvmaf/libvmaf.h"
#include "libvmaf/libvmaf_cuda.h"
#include "libvmaf/picture.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy also proposes the `nullptr` keyword, but MSVC's
 * documented /std:clatest C23 feature set does not include `nullptr` while the
 * required Windows build compiles this TU with cl.exe. ADR-1138. */

#define FRAME_W 192u
#define FRAME_H 128u
#define NUM_FRAMES 4u
#define NUM_KEYS 3u

static const char *const KEYS[NUM_KEYS] = {"psnr_y", "psnr_cb", "psnr_cr"};

/* lowbias32 hash: stateless, so both runs see the same pictures. */
static uint32_t sample_hash(unsigned row, unsigned col, unsigned plane, unsigned frame)
{
    uint32_t x =
        ((uint32_t)row << 16) ^ (uint32_t)col ^ (frame * 0x9E3779B9u) ^ (plane * 0x85EBCA6Bu);
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

/* Every plane is noise; `dist` adds a smaller noise to it, so each plane has a
 * finite PSNR well below the cap. */
static int fill_picture(VmafPicture *pic, unsigned frame, int distorted)
{
    const int err = vmaf_picture_alloc(pic, VMAF_PIX_FMT_YUV420P, 8, FRAME_W, FRAME_H);
    if (err)
        return err;
    for (unsigned p = 0; p < 3u; p++) {
        for (unsigned row = 0; row < pic->h[p]; row++) {
            uint8_t *line = (uint8_t *)pic->data[p] + ((size_t)row * pic->stride[p]);
            for (unsigned col = 0; col < pic->w[p]; col++) {
                uint32_t v = sample_hash(row, col, p, frame) >> 24;
                if (distorted)
                    v = (v + (sample_hash(row, col, p + 3u, frame) >> 27)) & 0xFFu;
                line[col] = (uint8_t)v;
            }
        }
    }
    return 0;
}

static int read_scores(VmafContext *vmaf, double out[NUM_FRAMES][NUM_KEYS])
{
    for (unsigned i = 0; i < NUM_FRAMES; i++) {
        for (unsigned k = 0; k < NUM_KEYS; k++) {
            const int err = vmaf_feature_score_at_index(vmaf, KEYS[k], &out[i][k], i);
            if (err)
                return err;
        }
    }
    return 0;
}

/* The CPU reference: host pictures, no GPU. */
static int score_on_cpu(double out[NUM_FRAMES][NUM_KEYS])
{
    const VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    VmafContext *vmaf = NULL;
    int err = vmaf_init(&vmaf, cfg);
    if (!err)
        err = vmaf_use_feature(vmaf, "psnr", NULL);
    for (unsigned i = 0; i < NUM_FRAMES && !err; i++) {
        VmafPicture ref;
        VmafPicture dist;
        err = fill_picture(&ref, i, 0);
        if (!err)
            err = fill_picture(&dist, i, 1);
        if (!err)
            err = vmaf_read_pictures(vmaf, &ref, &dist, i);
    }
    if (!err)
        err = vmaf_read_pictures(vmaf, NULL, NULL, 0);
    if (!err)
        err = read_scores(vmaf, out);
    if (vmaf) {
        const int close_err = vmaf_close(vmaf);
        err = err ? err : close_err;
    }
    return err;
}

/* Fetch a device picture and fill it from a freshly generated host picture. */
static int fetch_device_picture(VmafContext *vmaf, const VmafCudaState *state, unsigned frame,
                                int distorted, VmafPicture *dev)
{
    VmafPicture host;
    int err = fill_picture(&host, frame, distorted);
    if (err)
        return err;
    err = vmaf_cuda_fetch_preallocated_picture(vmaf, dev);
    if (!err)
        err = vmaf_cuda_picture_upload_async(dev, &host, 0x7);
    if (!err && state->f->cuStreamSynchronize(vmaf_cuda_picture_get_stream(dev)) != CUDA_SUCCESS)
        err = -EIO;
    const int unref_err = vmaf_picture_unref(&host);
    return err ? err : unref_err;
}

/* The CUDA run: device pictures, a CPU `psnr` (gpumask 1). */
static int score_from_device(unsigned n_threads, VmafCudaState *state,
                             double out[NUM_FRAMES][NUM_KEYS])
{
    const VmafConfiguration cfg = {
        .log_level = VMAF_LOG_LEVEL_NONE, .n_threads = n_threads, .gpumask = 1u};
    VmafContext *vmaf = NULL;
    int err = vmaf_init(&vmaf, cfg);
    if (!err)
        err = vmaf_cuda_import_state(vmaf, state);
    const VmafCudaPictureConfiguration pool = {
        .pic_params = {.w = FRAME_W, .h = FRAME_H, .bpc = 8, .pix_fmt = VMAF_PIX_FMT_YUV420P},
        .pic_prealloc_method = VMAF_CUDA_PICTURE_PREALLOCATION_METHOD_DEVICE,
    };
    if (!err)
        err = vmaf_cuda_preallocate_pictures(vmaf, pool);
    if (!err)
        err = vmaf_use_feature(vmaf, "psnr", NULL);
    for (unsigned i = 0; i < NUM_FRAMES && !err; i++) {
        VmafPicture ref;
        VmafPicture dist;
        err = fetch_device_picture(vmaf, state, i, 0, &ref);
        if (!err)
            err = fetch_device_picture(vmaf, state, i, 1, &dist);
        if (!err)
            err = vmaf_read_pictures(vmaf, &ref, &dist, i);
    }
    if (!err)
        err = vmaf_read_pictures(vmaf, NULL, NULL, 0);
    if (!err)
        err = read_scores(vmaf, out);
    if (vmaf) {
        const int close_err = vmaf_close(vmaf);
        err = err ? err : close_err;
    }
    return err;
}

static char *compare_with_cpu(unsigned n_threads, VmafCudaState *state)
{
    static double cpu[NUM_FRAMES][NUM_KEYS];
    static double dev[NUM_FRAMES][NUM_KEYS];
    mu_assert("the CPU reference run failed", score_on_cpu(cpu) == 0);
    const int err = score_from_device(n_threads, state, dev);
    if (err) {
        (void)fprintf(stderr, "\n  device-input run (threads=%u) failed with %d\n", n_threads, err);
        return "the device-input run failed";
    }
    for (unsigned i = 0; i < NUM_FRAMES; i++) {
        for (unsigned k = 0; k < NUM_KEYS; k++) {
            if (cpu[i][k] != dev[i][k]) {
                (void)fprintf(stderr, "\n  threads=%u frame %u %s: cpu=%.17g device-input=%.17g\n",
                              n_threads, i, KEYS[k], cpu[i][k], dev[i][k]);
                return "a CPU extractor scored device-resident input differently";
            }
        }
    }
    return NULL;
}

static char *test_cpu_extractor_sees_every_plane_of_device_input(void)
{
    VmafCudaState *state = NULL;
    const VmafCudaConfiguration cuda_cfg = {0};
    if (vmaf_cuda_state_init(&state, cuda_cfg) != 0 || state == NULL) {
        (void)fprintf(stderr, "[skip: no CUDA device] ");
        mu_skipped = 1;
        return NULL;
    }
    static const unsigned THREADS[] = {0u, 4u};
    char *msg = NULL;
    for (size_t t = 0; t < sizeof(THREADS) / sizeof(THREADS[0]) && !msg; t++) {
        VmafCudaState *run_state = NULL;
        if (t == 0) {
            run_state = state;
        } else if (vmaf_cuda_state_init(&run_state, cuda_cfg) != 0 || run_state == NULL) {
            msg = "could not create a second CUDA state";
            break;
        }
        msg = compare_with_cpu(THREADS[t], run_state);
        if (t != 0)
            (void)vmaf_cuda_state_free(run_state);
    }
    (void)vmaf_cuda_state_free(state);
    return msg;
}

char *run_tests(void)
{
    static const MuTest tests[] = {
        MU_TEST(test_cpu_extractor_sees_every_plane_of_device_input),
    };
    return mu_run_table(tests, MU_TABLE_LEN(tests));
}

/* NOLINTEND(modernize-use-nullptr) */
