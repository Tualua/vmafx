/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "config.h"
#include "test.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy also proposes the `nullptr` keyword, but MSVC's
 * documented /std:clatest C23 feature set does not include `nullptr` and the
 * required Windows builds compile this TU with cl.exe (C2065). ADR-1138. */

#if HAVE_SYCL

#include "libvmaf/libvmaf_sycl.h"
#include "feature/feature_extractor.h"

static VmafSyclState *sycl = NULL;
static int sycl_init_failed = 0;

static char *test_sycl_state_init(void)
{
    VmafSyclConfiguration cfg = {.device_index = -1};
    int err = vmaf_sycl_state_init(&sycl, cfg);
    if (err) {
        /* No SYCL GPU available — skip device-dependent tests */
        (void)fprintf(stderr,
                      "  [SKIP] SYCL state init failed (err=%d), "
                      "no GPU available — skipping device tests\n",
                      err);
        sycl_init_failed = 1;
        sycl = NULL;
        return NULL;
    }
    mu_assert("sycl_state should be non-NULL", sycl != NULL);
    return NULL;
}

static char *test_sycl_state_init_invalid(void)
{
    /* NULL pointer should be rejected */
    VmafSyclConfiguration cfg = {.device_index = -1};
    int err = vmaf_sycl_state_init(NULL, cfg);
    mu_assert("NULL pointer should return EINVAL", err < 0);

    /* Out-of-range device index */
    VmafSyclState *tmp = NULL;
    VmafSyclConfiguration bad_cfg = {.device_index = 9999};
    err = vmaf_sycl_state_init(&tmp, bad_cfg);
    mu_assert("invalid device_index should fail", err < 0);
    mu_assert("state should be NULL on failure", tmp == NULL);

    return NULL;
}

static char *test_sycl_import_state(void)
{
    if (sycl_init_failed) {
        (void)fprintf(stderr, "  [SKIP] test_sycl_import_state (no GPU)\n");
        return NULL;
    }

    VmafConfiguration vmaf_cfg = {
        .log_level = VMAF_LOG_LEVEL_NONE,
        .n_threads = 1,
    };
    VmafContext *vmaf = NULL;
    int err = vmaf_init(&vmaf, vmaf_cfg);
    mu_assert("vmaf_init should succeed", err == 0);

    err = vmaf_sycl_import_state(vmaf, sycl);
    mu_assert("vmaf_sycl_import_state should succeed", err == 0);

    vmaf_close(vmaf);
    return NULL;
}

static char *check_extractor_by_name(const char *name)
{
    VmafFeatureExtractor *fex = vmaf_get_feature_extractor_by_name(name);
    mu_assert("SYCL extractor should be registered", fex != NULL);
    mu_assert("SYCL extractor name should match", !strcmp(fex->name, name));
    return NULL;
}

static char *test_sycl_feature_extractor_lookup(void)
{
    /* Lookup by extractor name */
    mu_assert_msg(check_extractor_by_name("adm_sycl"));
    mu_assert_msg(check_extractor_by_name("vif_sycl"));
    mu_assert_msg(check_extractor_by_name("motion_sycl"));

    /* Lookup by feature name with SYCL flag */
    unsigned flags = VMAF_FEATURE_EXTRACTOR_SYCL;
    VmafFeatureExtractor *fex =
        vmaf_get_feature_extractor_by_feature_name("VMAF_integer_feature_adm2_score", flags);
    mu_assert("SYCL ADM should be found by feature name", fex != NULL);
    mu_assert("should be adm_sycl", !strcmp(fex->name, "adm_sycl"));

    return NULL;
}

static char *test_sycl_state_release(void)
{
    if (sycl_init_failed || sycl == NULL) {
        (void)fprintf(stderr, "  [SKIP] test_sycl_state_release (no GPU)\n");
        return NULL;
    }

    vmaf_sycl_state_free(&sycl);
    mu_assert("state should be NULL after free", sycl == NULL);

    return NULL;
}

char *run_tests(void)
{
    /* Invalid-argument tests work without a GPU */
    mu_run_test(test_sycl_state_init_invalid);

    /* Feature extractor registration is compile-time */
    mu_run_test(test_sycl_feature_extractor_lookup);

    /* Device-dependent tests (gracefully skipped if no GPU) */
    mu_run_test(test_sycl_state_init);
    mu_run_test(test_sycl_import_state);

    /* Cleanup (always last) */
    mu_run_test(test_sycl_state_release);

    return NULL;
}

#else /* !HAVE_SYCL */

char *run_tests(void)
{
    (void)fprintf(stderr, "SYCL not enabled, skipping tests\n");
    return NULL;
}

#endif /* HAVE_SYCL */

/* NOLINTEND(modernize-use-nullptr) */
