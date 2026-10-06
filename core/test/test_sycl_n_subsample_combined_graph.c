/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * n_subsample with SYCL extractors that share the combined graph
 * (T-SYCL-ZEROCOPY-NSUBSAMPLE-MOTION-2026-10-06).
 *
 * n_subsample skips every extractor without the TEMPORAL flag on the frames
 * it drops, so on those frames only the TEMPORAL motion_sycl submits. The
 * combined graph used to wait for every registered extractor and enqueued
 * nothing: motion_sycl scored a stale SAD and its next frame differenced
 * against a stale plane (integer_motion2 / 3 up to motion_max_val). The test
 * runs motion_sycl next to float_moment_sycl, a non-TEMPORAL extractor in the
 * same graph (psnr_sycl is TEMPORAL and would never be skipped), at
 * n_subsample 1, 2 and 4 and compares every motion output of every frame and
 * float_moment_ref1st of every scored frame with `==` against the CPU's
 * `motion` and `float_moment` under the same n_subsample. Frame counts 11 and 9 end
 * on a skipped and on a scored frame. meson runs the binary twice, the second
 * time with VMAF_SYCL_USE_GRAPH=1, so the recorded graph replays the full
 * frames and the skipped frames run directly between replays.
 *
 * Skip behaviour: exits 77 when there is no SYCL device.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "test.h"

#include "libvmaf/libvmaf.h"
#include "libvmaf/libvmaf_sycl.h"

#include "motion_five_frame_twin_parity.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy proposes `nullptr`, but the required MSVC C build does
 * not provide that keyword. Preserve the portable C spelling. ADR-1138. */

#define NSG_N_MOTION_KEYS 3u

static const char *const nsg_motion_keys[NSG_N_MOTION_KEYS] = {
    "VMAF_integer_feature_motion_sad_score", "VMAF_integer_feature_motion2_score",
    "VMAF_integer_feature_motion3_score"};

typedef struct NsgScores {
    double motion[NSG_N_MOTION_KEYS * MFT_MAX_FRAMES];
    double moment[MFT_MAX_FRAMES];
} NsgScores;

/* Frame `frame`: the motion fixture as reference, a later frame of it as
 * distorted picture. */
static int nsg_feed_frame(VmafContext *vmaf, unsigned frame)
{
    VmafPicture ref;
    VmafPicture dist;
    int err = mft_fill_picture(&ref, 8u, frame);
    if (err)
        return err;
    err = mft_fill_picture(&dist, 8u, frame + 1u);
    if (err) {
        (void)vmaf_picture_unref(&ref);
        return err;
    }
    return vmaf_read_pictures(vmaf, &ref, &dist, frame);
}

/* A context with `motion` and `float_moment`, or with their SYCL twins on
 * `state`. */
static int nsg_context(VmafContext **vmaf, VmafSyclState *state, unsigned n_subsample)
{
    const VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE, .n_subsample = n_subsample};
    int err = vmaf_init(vmaf, cfg);
    if (!err && state)
        err = vmaf_sycl_import_state(*vmaf, state);
    if (!err)
        err = vmaf_use_feature(*vmaf, state ? "motion_sycl" : "motion", NULL);
    if (!err)
        err = vmaf_use_feature(*vmaf, state ? "float_moment_sycl" : "float_moment", NULL);
    return err;
}

static int nsg_read_scores(VmafContext *vmaf, unsigned n_subsample, unsigned frames, NsgScores *out)
{
    int err = 0;
    for (unsigned i = 0; i < NSG_N_MOTION_KEYS * frames && !err; i++) {
        err = vmaf_feature_score_at_index(vmaf, nsg_motion_keys[i % NSG_N_MOTION_KEYS],
                                          &out->motion[i], i / NSG_N_MOTION_KEYS);
        if (err) {
            (void)fprintf(stderr, "\nno score for %s at frame %u\n",
                          nsg_motion_keys[i % NSG_N_MOTION_KEYS], i / NSG_N_MOTION_KEYS);
        }
    }
    for (unsigned f = 0; f < frames && !err; f += n_subsample) {
        err = vmaf_feature_score_at_index(vmaf, "float_moment_ref1st", &out->moment[f], f);
        if (err) {
            (void)fprintf(stderr, "\nno score for float_moment_ref1st at frame %u\n", f);
        }
    }
    return err;
}

static int nsg_run(VmafSyclState *state, unsigned n_subsample, unsigned frames, NsgScores *out)
{
    VmafContext *vmaf = NULL;
    int err = nsg_context(&vmaf, state, n_subsample);
    for (unsigned frame = 0; frame < frames && !err; frame++)
        err = nsg_feed_frame(vmaf, frame);
    if (!err)
        err = vmaf_read_pictures(vmaf, NULL, NULL, 0);
    if (!err)
        err = nsg_read_scores(vmaf, n_subsample, frames, out);
    const int closed = vmaf ? vmaf_close(vmaf) : 0;
    return err ? err : closed;
}

static unsigned nsg_mismatches(unsigned n_subsample, unsigned frames, const NsgScores *cpu,
                               const NsgScores *gpu)
{
    unsigned bad = 0u;
    bool moved = false;
    for (unsigned i = 0; i < NSG_N_MOTION_KEYS * frames; i++) {
        moved = moved || cpu->motion[i] != 0.0;
        if (!vmaf_test_identical_f64(cpu->motion[i], gpu->motion[i])) {
            bad++;
            (void)fprintf(stderr,
                          "\nn_subsample %u, %u frames, frame %u %s: cpu=%.17g sycl=%.17g\n",
                          n_subsample, frames, i / NSG_N_MOTION_KEYS,
                          nsg_motion_keys[i % NSG_N_MOTION_KEYS], cpu->motion[i], gpu->motion[i]);
        }
    }
    for (unsigned f = 0; f < frames; f += n_subsample) {
        if (!vmaf_test_identical_f64(cpu->moment[f], gpu->moment[f])) {
            bad++;
            (void)fprintf(stderr,
                          "\nn_subsample %u, %u frames, frame %u float_moment_ref1st: cpu=%.17g "
                          "sycl=%.17g\n",
                          n_subsample, frames, f, cpu->moment[f], gpu->moment[f]);
        }
    }
    /* The fixture moves: a run whose motion is 0 everywhere compared nothing. */
    return moved ? bad : bad + 1u;
}

/* Mismatches at one n_subsample and frame count; UINT32_MAX when a run
 * failed; 0 with mu_skipped set when there is no device. */
static unsigned nsg_case(unsigned n_subsample, unsigned frames)
{
    VmafSyclState *state = NULL;
    const VmafSyclConfiguration cfg = {.device_index = -1};
    if (vmaf_sycl_state_init(&state, cfg) != 0 || state == NULL) {
        (void)fprintf(stderr, "[skip: no SYCL device] ");
        mu_skipped = 1;
        return 0u;
    }
    NsgScores cpu = {{0.0}, {0.0}};
    NsgScores gpu = {{0.0}, {0.0}};
    const int gpu_err = nsg_run(state, n_subsample, frames, &gpu);
    vmaf_sycl_state_free(&state);
    const int cpu_err = gpu_err ? 0 : nsg_run(NULL, n_subsample, frames, &cpu);
    if (gpu_err || cpu_err) {
        (void)fprintf(stderr, "\nn_subsample %u, %u frames: run failed (sycl %d, cpu %d)\n",
                      n_subsample, frames, gpu_err, cpu_err);
        return UINT32_MAX;
    }
    return nsg_mismatches(n_subsample, frames, &cpu, &gpu);
}

static char *test_every_frame(void)
{
    mu_assert("motion_sycl / float_moment_sycl differ from the CPU at n_subsample 1",
              nsg_case(1u, MFT_MAX_FRAMES) == 0u);
    return NULL;
}

static char *test_n_subsample_2(void)
{
    mu_assert("motion_sycl / float_moment_sycl differ from the CPU at n_subsample 2, 11 frames",
              nsg_case(2u, MFT_MAX_FRAMES) == 0u);
    mu_assert("motion_sycl / float_moment_sycl differ from the CPU at n_subsample 2, 9 frames",
              nsg_case(2u, 9u) == 0u);
    return NULL;
}

static char *test_n_subsample_4(void)
{
    mu_assert("motion_sycl / float_moment_sycl differ from the CPU at n_subsample 4, 11 frames",
              nsg_case(4u, MFT_MAX_FRAMES) == 0u);
    mu_assert("motion_sycl / float_moment_sycl differ from the CPU at n_subsample 4, 9 frames",
              nsg_case(4u, 9u) == 0u);
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_every_frame);
    if (!mu_skipped)
        mu_run_test(test_n_subsample_2);
    if (!mu_skipped)
        mu_run_test(test_n_subsample_4);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
