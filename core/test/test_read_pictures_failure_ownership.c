/**
 * Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * vmaf_read_pictures() owns the pictures it is given whatever it returns
 * (Netflix/vmaf#1420, ADR-1431).
 *
 * Before ADR-1431 a call that failed before it reached an extractor (an index
 * that does not increase, pictures whose shape disagrees with the stream, a
 * flushed context, an out-of-memory in the picture pool) returned without
 * releasing the pictures. Pictures from the context's pool then stayed out of
 * it, the next vmaf_fetch_preallocated_picture() waited for one, and the CLI
 * hung in vmaf_close(). The pool here holds four pictures: the context keeps
 * the reference pictures of the last two frames it scored, which leaves one
 * pair for the next call. A picture that is not released shows up as a fetch
 * that never returns; an alarm turns that into a failed run instead of a
 * hang.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "mu_table.h"
#include "test.h"

#include "libvmaf/libvmaf.h"
#include "libvmaf/picture.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy also proposes the `nullptr` keyword, but MSVC's
 * documented /std:clatest C23 feature set does not include `nullptr` while the
 * required Windows build compiles this TU with cl.exe. ADR-1138. */

#define POOL_W 64u
#define POOL_H 64u
#define WATCHDOG_SECONDS 30u

/* A context with a pool of four pictures: two kept as the previous reference
 * pictures, one pair for the call under test. */
static VmafContext *pooled_context(void)
{
    const VmafConfiguration cfg = {.log_level = VMAF_LOG_LEVEL_NONE};
    VmafContext *vmaf = NULL;
    if (vmaf_init(&vmaf, cfg) != 0)
        return NULL;
    const VmafPictureConfiguration pool = {
        .pic_params = {.w = POOL_W, .h = POOL_H, .bpc = 8, .pix_fmt = VMAF_PIX_FMT_YUV420P},
        .pic_cnt = 4,
    };
    if (vmaf_preallocate_pictures(vmaf, pool) != 0) {
        (void)vmaf_close(vmaf);
        return NULL;
    }
    return vmaf;
}

static int fetch_pair(VmafContext *vmaf, VmafPicture *ref, VmafPicture *dist)
{
    const int err = vmaf_fetch_preallocated_picture(vmaf, ref);
    return err ? err : vmaf_fetch_preallocated_picture(vmaf, dist);
}

/* Submit one pooled pair at `index`; the context owns it afterwards. */
static int submit_pooled(VmafContext *vmaf, unsigned index)
{
    VmafPicture ref;
    VmafPicture dist;
    const int err = fetch_pair(vmaf, &ref, &dist);
    return err ? err : vmaf_read_pictures(vmaf, &ref, &dist, index);
}

/* `count` pooled pairs from `first` on; with every picture returned this
 * never waits, with one missing it does within a few frames. */
static int submit_run(VmafContext *vmaf, unsigned first, unsigned count)
{
    for (unsigned i = 0; i < count; i++) {
        const int err = submit_pooled(vmaf, first + i);
        if (err)
            return err;
    }
    return 0;
}

static void arm_watchdog(void)
{
#ifndef _WIN32
    (void)alarm(WATCHDOG_SECONDS);
#endif
}

static void disarm_watchdog(void)
{
#ifndef _WIN32
    (void)alarm(0);
#endif
}

static char *test_rejected_index_releases_the_pictures(void)
{
    VmafContext *vmaf = pooled_context();
    mu_assert("init failed", vmaf != NULL);
    arm_watchdog();

    mu_assert("frame 5 rejected", submit_pooled(vmaf, 5) == 0);
    /* A repeated index fails; the pair it carried has to be back in the pool,
     * or the fetches below never return. */
    mu_assert("a repeated index was accepted", submit_pooled(vmaf, 5) == -EINVAL);
    mu_assert("the pair of a rejected index did not return to the pool",
              submit_run(vmaf, 6, 3u) == 0);
    mu_assert("an earlier index was accepted", submit_pooled(vmaf, 2) == -EINVAL);
    mu_assert("the pair of an earlier index did not return to the pool",
              submit_run(vmaf, 9, 3u) == 0);

    disarm_watchdog();
    mu_assert("close failed", vmaf_close(vmaf) == 0);
    return NULL;
}

static char *test_mismatched_pictures_release_the_pictures(void)
{
    VmafContext *vmaf = pooled_context();
    mu_assert("init failed", vmaf != NULL);
    arm_watchdog();

    VmafPicture ref;
    VmafPicture dist;
    mu_assert("fetch failed", vmaf_fetch_preallocated_picture(vmaf, &ref) == 0);
    /* The distorted picture has another size than the reference. */
    mu_assert("alloc failed",
              vmaf_picture_alloc(&dist, VMAF_PIX_FMT_YUV420P, 8, POOL_W / 2u, POOL_H / 2u) == 0);
    mu_assert("pictures of different sizes were accepted",
              vmaf_read_pictures(vmaf, &ref, &dist, 0) == -EINVAL);
    mu_assert("the pooled picture did not return to the pool", submit_run(vmaf, 0, 4u) == 0);

    disarm_watchdog();
    mu_assert("close failed", vmaf_close(vmaf) == 0);
    return NULL;
}

static char *test_flushed_context_releases_the_pictures(void)
{
    VmafContext *vmaf = pooled_context();
    mu_assert("init failed", vmaf != NULL);
    arm_watchdog();

    mu_assert("frame 0 rejected", submit_pooled(vmaf, 0) == 0);
    mu_assert("flush failed", vmaf_read_pictures(vmaf, NULL, NULL, 0) == 0);
    mu_assert("a flushed context accepted a pair", submit_pooled(vmaf, 1) == -EINVAL);
    /* The pair is back in the pool: further pairs can be fetched. */
    for (unsigned i = 0; i < 4u; i++) {
        VmafPicture ref;
        VmafPicture dist;
        mu_assert("the pair of a flushed context did not return to the pool",
                  fetch_pair(vmaf, &ref, &dist) == 0);
        mu_assert("unref ref failed", vmaf_picture_unref(&ref) == 0);
        mu_assert("unref dist failed", vmaf_picture_unref(&dist) == 0);
    }

    disarm_watchdog();
    mu_assert("close failed", vmaf_close(vmaf) == 0);
    return NULL;
}

char *run_tests(void)
{
    static const MuTest tests[] = {
        MU_TEST(test_rejected_index_releases_the_pictures),
        MU_TEST(test_mismatched_pictures_release_the_pictures),
        MU_TEST(test_flushed_context_releases_the_pictures),
    };
    return mu_run_table(tests, MU_TABLE_LEN(tests));
}

/* NOLINTEND(modernize-use-nullptr) */
