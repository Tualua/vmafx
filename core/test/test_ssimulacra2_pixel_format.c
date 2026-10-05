/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * feature/ssimulacra2_pixel_format.h: the one pixel-format check of the CPU
 * ssimulacra2 extractor and of its CUDA, SYCL, HIP and Metal twins
 * (T-METAL-SSIMULACRA2-YUV400-ACCEPTED-2026-10-05). 4:2:0, 4:2:2 and 4:4:4
 * pass without a message; 4:0:0 and an unknown format get -EINVAL and the CPU
 * extractor's error line, named after the extractor that refused.
 *
 * vmaf_log() is this file's own: it keeps the last message, so the test reads
 * the text a user would see. test_ssimulacra2_pixel_format_contract.py checks
 * that every extractor's init() calls the helper.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "feature/ssimulacra2_pixel_format.h"
#include "libvmaf/libvmaf.h"
#include "libvmaf/picture.h"
#include "log.h"
#include "test.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit; MSVC's documented
 * /std:clatest C23 surface does not include nullptr (ADR-1138). */

static char last_message[256];
static unsigned message_count;
static enum VmafLogLevel last_level;

void vmaf_log(enum VmafLogLevel log_level, const char *fmt, ...)
{
    va_list args;
#if defined(__clang__) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
    /* As in core/src/log.c: clang lowers the C23 va_start macro to
     * __builtin_c23_va_start, which its VAList analyzer does not model yet;
     * the traditional builtin initialises the list the same way. */
    __builtin_va_start(args, fmt);
#else
    va_start(args, fmt);
#endif
    const int n = vsnprintf(last_message, sizeof(last_message), fmt, args);
    va_end(args);
    if (n < 0)
        last_message[0] = '\0';
    last_level = log_level;
    message_count++;
}

static void reset_log(void)
{
    last_message[0] = '\0';
    message_count = 0u;
    last_level = VMAF_LOG_LEVEL_NONE;
}

static char *test_chroma_formats_pass_silently(void)
{
    static const enum VmafPixelFormat formats[] = {VMAF_PIX_FMT_YUV420P, VMAF_PIX_FMT_YUV422P,
                                                   VMAF_PIX_FMT_YUV444P};
    for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
        reset_log();
        mu_assert("4:2:0, 4:2:2 and 4:4:4 have chroma", vmaf_ss2_has_chroma(formats[i]));
        mu_assert("4:2:0, 4:2:2 and 4:4:4 pass the check",
                  vmaf_ss2_check_pixel_format(formats[i], "ssimulacra2") == 0);
        mu_assert("a format that passes logs nothing", message_count == 0u);
    }
    return NULL;
}

/* The CPU extractor's message, unchanged since T-SSIMULACRA2-CPU-YUV400-NULL-
 * CHROMA-2026-10-01 (docs/metrics/ssimulacra2.md quotes it). */
static char *test_yuv400_refused_with_cpu_message(void)
{
    reset_log();
    mu_assert("4:0:0 has no chroma", !vmaf_ss2_has_chroma(VMAF_PIX_FMT_YUV400P));
    mu_assert("4:0:0 is refused with -EINVAL",
              vmaf_ss2_check_pixel_format(VMAF_PIX_FMT_YUV400P, "ssimulacra2") == -EINVAL);
    mu_assert("one message per refusal", message_count == 1u);
    mu_assert("the refusal is an error", last_level == VMAF_LOG_LEVEL_ERROR);
    mu_assert("the CPU extractor's message is unchanged",
              strcmp(last_message,
                     "ssimulacra2: needs a YUV 4:2:0, 4:2:2 or 4:4:4 input, not 4:0:0\n") == 0);
    return NULL;
}

static char *test_unknown_format_refused(void)
{
    reset_log();
    mu_assert("an unknown format has no chroma", !vmaf_ss2_has_chroma(VMAF_PIX_FMT_UNKNOWN));
    mu_assert("an unknown format is refused with -EINVAL",
              vmaf_ss2_check_pixel_format(VMAF_PIX_FMT_UNKNOWN, "ssimulacra2") == -EINVAL);
    mu_assert("one message per refusal", message_count == 1u);
    return NULL;
}

/* A twin's refusal names the twin, so a log says which backend refused. */
static char *test_refusal_names_the_extractor(void)
{
    reset_log();
    mu_assert("the Metal twin refuses 4:0:0",
              vmaf_ss2_check_pixel_format(VMAF_PIX_FMT_YUV400P, "ssimulacra2_metal") == -EINVAL);
    mu_assert("the message names the Metal twin",
              strcmp(last_message, "ssimulacra2_metal: needs a YUV 4:2:0, 4:2:2 or 4:4:4 input, "
                                   "not 4:0:0\n") == 0);
    return NULL;
}

/* Every enumerator: the check and the context checks' predicate agree. */
static char *test_check_agrees_with_has_chroma(void)
{
    for (int f = (int)VMAF_PIX_FMT_UNKNOWN; f <= (int)VMAF_PIX_FMT_YUV400P; f++) {
        const enum VmafPixelFormat fmt = (enum VmafPixelFormat)f;
        reset_log();
        const int err = vmaf_ss2_check_pixel_format(fmt, "ssimulacra2");
        mu_assert("the check accepts exactly the formats with chroma",
                  (err == 0) == vmaf_ss2_has_chroma(fmt));
        mu_assert("a refusal is -EINVAL", err == 0 || err == -EINVAL);
    }
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_chroma_formats_pass_silently);
    mu_run_test(test_yuv400_refused_with_cpu_message);
    mu_run_test(test_unknown_format_refused);
    mu_run_test(test_refusal_names_the_extractor);
    mu_run_test(test_check_agrees_with_has_chroma);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
