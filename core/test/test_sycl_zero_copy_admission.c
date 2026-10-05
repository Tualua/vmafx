/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * Which extractors the SYCL zero-copy path admits (ADR-1688, widened by
 * ADR-1768).
 *
 * vmaf_read_pictures_sycl() hands the extractors no host picture. The DMA-BUF /
 * VA import fills the shared luma and, since ADR-1765, chroma planes, and every
 * SYCL twin reads those planes (ADR-1766, ADR-1767). It admits a registered
 * extractor only when vmaf_feature_extractor_reads_shared_luma_only() answers
 * true, so that answer must be the truth for every SYCL extractor and every
 * option that changes it:
 * - every SYCL twin answers true, with any chroma option (motion_add_uv,
 *   enable_chroma): a chroma reader on an import that carried no chroma is
 *   refused later, at submit(), by vmaf_sycl_require_chroma();
 * - a CPU extractor answers false (the zero-copy path skipped it on every
 *   frame, so its feature was missing from the result without an error).
 *
 * Device-free: the answers come from the parsed options, before init().
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "test.h"

#include "dict.h"
#include "feature/feature_extractor.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit; MSVC's C mode has
 * no `nullptr`. ADR-1138. */

/* The answer of extractor `name` with option `key=val` (no option when key is
 * NULL), in *out. 0 on success. */
static int admitted(const char *name, const char *key, const char *val, bool *out)
{
    VmafFeatureExtractor *fex = vmaf_get_feature_extractor_by_name(name);
    if (fex == NULL) {
        return -ENOENT;
    }
    VmafDictionary *opts = NULL;
    if (key != NULL && vmaf_dictionary_set(&opts, key, val, 0) != 0) {
        return -ENOMEM;
    }
    VmafFeatureExtractorContext *ctx = NULL;
    const int err = vmaf_feature_extractor_context_create(&ctx, fex, opts);
    if (err) {
        return err;
    }
    *out = vmaf_feature_extractor_reads_shared_luma_only(ctx->fex);
    return vmaf_feature_extractor_context_destroy(ctx);
}

typedef struct AdmissionCase {
    const char *name, *key, *val;
    bool expect;
} AdmissionCase;

static int case_holds(const AdmissionCase *c)
{
    bool got = !c->expect;
    const int err = admitted(c->name, c->key, c->val, &got);
    if (err || got != c->expect) {
        (void)fprintf(stderr, "%s%s%s%s: err %d, admitted %d, expected %d\n", c->name,
                      c->key ? " " : "", c->key ? c->key : "", c->key ? "=..." : "", err, got,
                      c->expect);
        return 0;
    }
    return 1;
}

static char *run_cases(const AdmissionCase *cases, size_t n)
{
    int ok = 1;
    for (size_t i = 0u; i < n; i++) {
        if (!case_holds(&cases[i])) {
            ok = 0;
        }
    }
    mu_assert("an extractor's zero-copy answer is not the expected one", ok);
    return NULL;
}

static char *test_luma_twins_are_admitted(void)
{
    static const AdmissionCase cases[] = {
        {"adm_sycl", NULL, NULL, true},          {"cambi_sycl", NULL, NULL, true},
        {"float_moment_sycl", NULL, NULL, true}, {"motion_v2_sycl", NULL, NULL, true},
        {"vif_sycl", NULL, NULL, true},          {"motion_sycl", NULL, NULL, true},
    };
    return run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}

static char *test_chroma_options_are_admitted(void)
{
    static const AdmissionCase cases[] = {
        {"motion_sycl", "motion_add_uv", "true", true},
        {"motion_sycl", "motion_add_uv", "false", true},
        {"psnr_sycl", NULL, NULL, true},
        {"psnr_sycl", "enable_chroma", "true", true},
        {"psnr_sycl", "enable_chroma", "false", true},
        {"psnr_hvs_sycl", NULL, NULL, true},
        {"psnr_hvs_sycl", "enable_chroma", "false", true},
        {"float_motion_sycl", "motion_add_uv", "true", true},
    };
    return run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}

static char *test_shared_plane_twins_are_admitted(void)
{
    static const AdmissionCase cases[] = {
        {"speed_chroma_sycl", NULL, NULL, true},  {"speed_temporal_sycl", NULL, NULL, true},
        {"ciede_sycl", NULL, NULL, true},         {"ssimulacra2_sycl", NULL, NULL, true},
        {"float_ms_ssim_sycl", NULL, NULL, true}, {"float_ssim_sycl", NULL, NULL, true},
        {"integer_ssim_sycl", NULL, NULL, true},  {"float_psnr_sycl", NULL, NULL, true},
        {"float_motion_sycl", NULL, NULL, true},  {"float_vif_sycl", NULL, NULL, true},
        {"float_adm_sycl", NULL, NULL, true},
    };
    return run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}

static char *test_cpu_extractors_are_refused(void)
{
    static const AdmissionCase cases[] = {
        {"float_psnr", NULL, NULL, false},
        {"speed_chroma", NULL, NULL, false},
        {"adm", NULL, NULL, false},
    };
    mu_assert("no extractor is admitted", !vmaf_feature_extractor_reads_shared_luma_only(NULL));
    return run_cases(cases, sizeof(cases) / sizeof(cases[0]));
}

char *run_tests(void)
{
    mu_run_test(test_luma_twins_are_admitted);
    mu_run_test(test_chroma_options_are_admitted);
    mu_run_test(test_shared_plane_twins_are_admitted);
    mu_run_test(test_cpu_extractors_are_refused);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
