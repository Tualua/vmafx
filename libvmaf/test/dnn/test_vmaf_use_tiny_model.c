/**
 *  Copyright 2026 Lusoris and Claude (Anthropic)
 *  SPDX-License-Identifier: BSD-3-Clause-Plus-Patent
 *
 *  Public-surface tests for vmaf_use_tiny_model() — the ctx-attach path
 *  that was identified as having zero C-unit-test coverage in the 2026-05-16
 *  test-coverage audit (§2, "vmaf_use_tiny_model").
 *
 *  Strategy: link against libvmaf (not compiled-in dnn/ sources) so the test
 *  exercises the stable ABI contract the way downstream consumers do.
 *
 *  When built with -Denable_dnn=false:
 *    - vmaf_dnn_available() == 0
 *    - vmaf_use_tiny_model() must return -ENOSYS (ADR-0374 stub contract)
 *    - null-arg tests are skipped to avoid assert-abort on disabled builds
 *
 *  When built with -Denable_dnn=true:
 *    - null ctx  → -EINVAL
 *    - null path → -EINVAL
 *    - non-existent path → -ENOENT
 *    - smoke_v0.onnx happy path → 0 (session attached, ctx closed cleanly)
 */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "test.h"

#include "libvmaf/dnn.h"
#include "libvmaf/libvmaf.h"

/* Path is relative to workdir = project root (set in meson.build). */
#define SMOKE_FP32_MODEL "model/tiny/smoke_v0.onnx"
/* ADR-0524: minimal rank-4 NCHW ONNX with `dim_param='batch'` on dim 0.
 * Generated once and committed under model/tiny/; see
 * docs/adr/0524-tiny-model-loader-symbolic-batch-dim.md. The loader
 * folds symbolic batch to 1; attach must succeed even though
 * ORT reports `in_shape[0] == -1`. */
#define SMOKE_SYMBATCH_MODEL "model/tiny/smoke_v0_symbolic_batch.onnx"

/* Helper: allocate a minimal VmafContext (no features, no threads). */
static VmafContext *alloc_ctx(void)
{
    VmafConfiguration cfg = {
        .log_level = VMAF_LOG_LEVEL_NONE,
        .n_threads = 1,
        .n_subsample = 1,
        .cpumask = 0,
        .gpumask = 0,
    };
    VmafContext *ctx = NULL;
    int rc = vmaf_init(&ctx, cfg);
    if (rc < 0)
        return NULL;
    return ctx;
}

/* --- stub contract when DNN is disabled ----------------------------------- */

static char *test_returns_enosys_when_disabled(void)
{
    if (vmaf_dnn_available())
        return NULL; /* real ORT — skip this test */

    int rc = vmaf_use_tiny_model(NULL, SMOKE_FP32_MODEL, NULL);
    mu_assert("disabled build: must return -ENOSYS regardless of args", rc == -ENOSYS);
    return NULL;
}

/* --- null-arg rejection (enabled build only) ------------------------------ */

static char *test_rejects_null_ctx(void)
{
    if (!vmaf_dnn_available())
        return NULL; /* stub build — skip */

    int rc = vmaf_use_tiny_model(NULL, SMOKE_FP32_MODEL, NULL);
    mu_assert("null ctx must return -EINVAL", rc == -EINVAL);
    return NULL;
}

static char *test_rejects_null_path(void)
{
    if (!vmaf_dnn_available())
        return NULL;

    VmafContext *ctx = alloc_ctx();
    mu_assert("vmaf_init must succeed", ctx != NULL);

    int rc = vmaf_use_tiny_model(ctx, NULL, NULL);
    mu_assert("null path must return -EINVAL", rc == -EINVAL);

    (void)vmaf_close(ctx);
    return NULL;
}

/* --- non-existent path (enabled build only) ------------------------------- */

static char *test_rejects_nonexistent_path(void)
{
    if (!vmaf_dnn_available())
        return NULL;

    VmafContext *ctx = alloc_ctx();
    mu_assert("vmaf_init must succeed", ctx != NULL);

    int rc = vmaf_use_tiny_model(ctx, "/nonexistent/__no_such_file__.onnx", NULL);
    mu_assert("non-existent path must return < 0", rc < 0);

    (void)vmaf_close(ctx);
    return NULL;
}

/* --- happy path (enabled build + smoke fixture present) ------------------- */

static char *test_happy_path_smoke_model(void)
{
    if (!vmaf_dnn_available())
        return NULL;

    /* smoke_v0.onnx may not be present in every CI leg that runs the dnn
     * suite (e.g. MSVC build-only, cross builds without model fixtures). */
#ifndef _WIN32
    if (access(SMOKE_FP32_MODEL, R_OK) != 0)
        return NULL;
#endif

    VmafContext *ctx = alloc_ctx();
    mu_assert("vmaf_init must succeed", ctx != NULL);

    int rc = vmaf_use_tiny_model(ctx, SMOKE_FP32_MODEL, NULL);
    mu_assert("smoke model attach must return 0", rc == 0);

    /* vmaf_close tears down the DNN session via vmaf_ctx_dnn_attach's
     * ownership transfer — this exercises the teardown path as well. */
    rc = vmaf_close(ctx);
    mu_assert("vmaf_close after tiny model must return 0", rc == 0);
    return NULL;
}

/* --- ADR-0524: symbolic batch dim acceptance ------------------------------ */

/* The shipped NR tiny checkpoints (model/tiny/nr_metric_v1*.onnx) all
 * declare their NCHW input shape as `['batch', 1, 224, 224]` — the
 * ONNX `dim_param='batch'` token, which ORT surfaces through the C
 * API as `-1`. Before ADR-0523 the loader rejected this with -ENOTSUP
 * (errno 95) because `dnn_attach_nchw` required `in_shape[0] == 1`.
 * The fixture `smoke_v0_symbolic_batch.onnx` is the minimal
 * reproducer: a rank-4 Identity graph with a symbolic batch on dim 0
 * and concrete C=1 / H=4 / W=4. Attach must succeed; the per-frame
 * inference loop always emits batch=1 so symbolic batch is safe to
 * fold to 1 at attach time. */
static char *test_attach_accepts_symbolic_batch_rank4(void)
{
    if (!vmaf_dnn_available())
        return NULL;

#ifndef _WIN32
    if (access(SMOKE_SYMBATCH_MODEL, R_OK) != 0)
        return NULL;
#endif

    VmafContext *ctx = alloc_ctx();
    mu_assert("vmaf_init must succeed", ctx != NULL);

    int rc = vmaf_use_tiny_model(ctx, SMOKE_SYMBATCH_MODEL, NULL);
    mu_assert("ADR-0524: symbolic-batch rank-4 model must attach (folded to batch=1)", rc == 0);

    rc = vmaf_close(ctx);
    mu_assert("vmaf_close after symbolic-batch attach must return 0", rc == 0);
    return NULL;
}

/* -------------------------------------------------------------------------- */

char *run_tests(void)
{
    mu_run_test(test_returns_enosys_when_disabled);
    mu_run_test(test_rejects_null_ctx);
    mu_run_test(test_rejects_null_path);
    mu_run_test(test_rejects_nonexistent_path);
    mu_run_test(test_happy_path_smoke_model);
    mu_run_test(test_attach_accepts_symbolic_batch_rank4);
    return NULL;
}
