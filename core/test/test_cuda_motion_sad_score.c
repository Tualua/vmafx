/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * motion_cuda emits every output of the CPU `motion` extractor, with the
 * CPU's bits (the twin carries the CPU's contract, ADR-1373).
 *
 * integer_motion.c appends `VMAF_integer_feature_motion_sad_score` on every
 * frame: the frame's SAD, weighted by motion_fps_weight and capped at
 * motion_max_val, 0 on the first frame and under motion_force_zero. It is the
 * value motion2 and motion3 are derived from, and with debug=true the same
 * value is repeated as `motion_score`. motion_cuda computed it and published
 * it only as the debug score, so a run on CUDA lacked an output the same
 * request on the CPU has.
 *
 * Each case feeds eleven moving frames to the CPU extractor and to the twin
 * and compares every output of every frame with ==. Eleven frames cross the
 * twin's batch boundary (eight frames per readback, ADR-0845) and leave a
 * tail for flush, so both emit paths are covered. On the old twin every case
 * fails: it has no SAD score to read.
 *
 * Skip behaviour: exits 77 when there is no CUDA device.
 */

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "test.h"

#include "libvmaf/libvmaf.h"
#include "libvmaf/libvmaf_cuda.h"
#include "libvmaf/picture.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy proposes `nullptr`, but the required MSVC C build does
 * not provide that keyword. Preserve the portable C spelling. ADR-1138. */

#define FIXTURE_W 256u
#define FIXTURE_H 144u

/* More than one batch of the twin (MOTION_BATCH_DEPTH is 8) plus a tail. */
#define NUM_FRAMES 11u
#define MAX_KEYS 4u
#define MAX_OPTS 3u

typedef struct MotionOption {
    const char *key;
    const char *val;
} MotionOption;

/* One option set and the outputs the CPU extractor has under it. */
typedef struct MotionCase {
    const char *name;
    MotionOption opts[MAX_OPTS]; /* NULL-key terminated */
    bool nonzero;                /* the SAD score must not be 0 on every frame */
    const char *keys[MAX_KEYS];  /* NULL-terminated; the SAD score first */
} MotionCase;

static const MotionCase cases[] = {
    {"default",
     {{NULL, NULL}},
     true,
     {"VMAF_integer_feature_motion_sad_score", "VMAF_integer_feature_motion2_score",
      "VMAF_integer_feature_motion3_score"}},
    {"debug",
     {{"debug", "true"}},
     true,
     {"VMAF_integer_feature_motion_sad_score", "VMAF_integer_feature_motion_score",
      "VMAF_integer_feature_motion2_score", "VMAF_integer_feature_motion3_score"}},
    {"force zero",
     {{"motion_force_zero", "true"}},
     false,
     {"VMAF_integer_feature_motion_sad_score_force_0", "integer_motion2_force_0",
      "integer_motion3_force_0"}},
    {"weight and cap",
     {{"debug", "true"}, {"motion_fps_weight", "0.3"}, {"motion_max_val", "0.5"}},
     true,
     {"VMAF_integer_feature_motion_sad_score_mfw_0.3_mmxv_0.5", "integer_motion_mfw_0.3_mmxv_0.5",
      "integer_motion2_mfw_0.3_mmxv_0.5", "integer_motion3_mfw_0.3_mmxv_0.5"}},
};
#define N_CASES (sizeof(cases) / sizeof(cases[0]))

static size_t key_count(const MotionCase *c)
{
    size_t n = 0u;
    while (n < MAX_KEYS && c->keys[n] != NULL) {
        n++;
    }
    return n;
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

/* Texture that moves down three rows and right one column a frame, with a
 * step that grows with the frame so consecutive SADs differ. */
static unsigned luma(unsigned row, unsigned col, unsigned frame, unsigned bpc)
{
    const unsigned r = row + (frame * 3u);
    const unsigned c = col + frame;
    const unsigned v = (((r * 3u) + (c * 2u)) & 0xFFu) ^ (((r >> 2) * (c >> 3)) & 0x1Fu);
    return ((v + (frame * frame)) & 0xFFu) << (bpc - 8u);
}

static int fill_picture(VmafPicture *pic, unsigned bpc, unsigned frame)
{
    const int err = vmaf_picture_alloc(pic, VMAF_PIX_FMT_YUV420P, bpc, FIXTURE_W, FIXTURE_H);
    if (err) {
        return err;
    }
    for (unsigned row = 0; row < pic->h[0]; row++) {
        for (unsigned col = 0; col < pic->w[0]; col++) {
            put_sample(pic, 0u, row, col, luma(row, col, frame, bpc));
        }
    }
    for (unsigned p = 1; p < 3u; p++) {
        for (unsigned row = 0; row < pic->h[p]; row++) {
            for (unsigned col = 0; col < pic->w[p]; col++) {
                put_sample(pic, p, row, col, 1u << (bpc - 1u));
            }
        }
    }
    return 0;
}

/* Frame `frame` through `vmaf`, which takes both pictures. Motion reads the
 * reference only; the distorted picture is the same frame. */
static int feed_frame(VmafContext *vmaf, unsigned bpc, unsigned frame)
{
    VmafPicture ref;
    VmafPicture dist;
    int err = fill_picture(&ref, bpc, frame);
    if (err) {
        return err;
    }
    err = fill_picture(&dist, bpc, frame);
    if (err) {
        (void)vmaf_picture_unref(&ref);
        return err;
    }
    return vmaf_read_pictures(vmaf, &ref, &dist, frame);
}

/* A context with the CPU `motion`, or `motion_cuda` on `cu_state`, under the
 * case's options. */
static int case_context(VmafContext **vmaf, const MotionCase *c, VmafCudaState *cu_state)
{
    const VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    VmafFeatureDictionary *opts = NULL;
    int err = vmaf_init(vmaf, cfg);
    if (!err && cu_state) {
        err = vmaf_cuda_import_state(*vmaf, cu_state);
    }
    for (unsigned i = 0; i < MAX_OPTS && !err && c->opts[i].key != NULL; i++) {
        err = vmaf_feature_dictionary_set(&opts, c->opts[i].key, c->opts[i].val);
    }
    if (!err) {
        /* vmaf_use_feature() takes the dictionary over, on failure too. */
        err = vmaf_use_feature(*vmaf, cu_state ? "motion_cuda" : "motion", opts);
    }
    return err;
}

/* NUM_FRAMES frames through one extractor, and every key of the case of every
 * frame read into `out` (frame-major). Returns the first error. */
static int case_scores(const MotionCase *c, VmafCudaState *cu_state, unsigned bpc, double *out)
{
    const size_t count = key_count(c);
    VmafContext *vmaf = NULL;
    int err = case_context(&vmaf, c, cu_state);
    for (unsigned frame = 0; frame < NUM_FRAMES && !err; frame++) {
        err = feed_frame(vmaf, bpc, frame);
    }
    if (!err) {
        err = vmaf_read_pictures(vmaf, NULL, NULL, 0);
    }
    for (size_t i = 0; i < count * NUM_FRAMES && !err; i++) {
        err = vmaf_feature_score_at_index(vmaf, c->keys[i % count], &out[i], (unsigned)(i / count));
        if (err) {
            (void)fprintf(stderr, "\n%s (%s): no score for %s at frame %u\n",
                          cu_state ? "motion_cuda" : "motion", c->name, c->keys[i % count],
                          (unsigned)(i / count));
        }
    }
    const int closed = vmaf ? vmaf_close(vmaf) : 0;
    return err ? err : closed;
}

/* The outputs whose CUDA value is not the CPU's, each one reported, plus one
 * when the SAD score is 0 on every frame of a case that must move. */
static unsigned count_mismatches(const MotionCase *c, unsigned bpc, const double *cpu,
                                 const double *gpu)
{
    const size_t count = key_count(c);
    unsigned mismatches = 0u;
    bool any_nonzero = false;
    for (size_t i = 0; i < count * NUM_FRAMES; i++) {
        any_nonzero = any_nonzero || ((i % count) == 0u && cpu[i] != 0.0);
        if (isfinite(cpu[i]) && cpu[i] == gpu[i]) {
            continue;
        }
        mismatches++;
        (void)fprintf(stderr, "\n%s %u-bit frame %u %s: cpu=%.17g cuda=%.17g delta=%.3e\n", c->name,
                      bpc, (unsigned)(i / count), c->keys[i % count], cpu[i], gpu[i],
                      fabs(cpu[i] - gpu[i]));
    }
    if (c->nonzero && !any_nonzero) {
        (void)fprintf(stderr, "\n%s %u-bit: %s is 0 on every frame\n", c->name, bpc, c->keys[0]);
        mismatches++;
    }
    return mismatches;
}

/* Mismatches of one case at one bit depth; UINT32_MAX when a run failed. A
 * skipped CUDA leg counts as 0. */
static unsigned case_mismatches(const MotionCase *c, unsigned bpc)
{
    double cpu[MAX_KEYS * NUM_FRAMES] = {0.0};
    double gpu[MAX_KEYS * NUM_FRAMES] = {0.0};
    VmafCudaState *cu_state = NULL;
    const VmafCudaConfiguration cuda_cfg = {0};
    if (vmaf_cuda_state_init(&cu_state, cuda_cfg) != 0 || cu_state == NULL) {
        (void)fprintf(stderr, "[skip: no CUDA device] ");
        mu_skipped = 1;
        return 0u;
    }
    const int gpu_err = case_scores(c, cu_state, bpc, gpu);
    const int free_err = vmaf_cuda_state_free(cu_state);
    const int cpu_err = gpu_err ? 0 : case_scores(c, NULL, bpc, cpu);
    if (gpu_err || cpu_err || free_err) {
        (void)fprintf(stderr, "\n%s %u-bit: run failed (cuda %d, cpu %d, free %d)\n", c->name, bpc,
                      gpu_err, cpu_err, free_err);
        return UINT32_MAX;
    }
    return count_mismatches(c, bpc, cpu, gpu);
}

/* Every case at one bit depth; all of them run, so one failure does not hide
 * the next. */
static unsigned mismatches_at(unsigned bpc)
{
    unsigned failed_cases = 0u;
    for (size_t i = 0; i < N_CASES && !mu_skipped; i++) {
        failed_cases += (case_mismatches(&cases[i], bpc) != 0u) ? 1u : 0u;
    }
    return failed_cases;
}

static char *test_motion_outputs_8bit(void)
{
    mu_assert("motion_cuda does not return every CPU motion output bit for bit at 8 bits",
              mismatches_at(8u) == 0u);
    return NULL;
}

static char *test_motion_outputs_10bit(void)
{
    mu_assert("motion_cuda does not return every CPU motion output bit for bit at 10 bits",
              mismatches_at(10u) == 0u);
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_motion_outputs_8bit);
    mu_run_test(test_motion_outputs_10bit);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
