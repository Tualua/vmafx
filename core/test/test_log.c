/**
 *
 *  Copyright 2016-2026 Netflix, Inc.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 *     Licensed under the BSD+Patent License (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         https://opensource.org/licenses/BSDplusPatent
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */

#include <stdint.h>

#include "test.h"
#include "log.h"
#include "libvmaf/libvmaf.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy also proposes the `nullptr` keyword, but MSVC's
 * documented /std:clatest C23 feature set does not include `nullptr`. This
 * test follows the cross-platform spelling of the surface it exercises.
 * ADR-1138. */

static char *test_vmaf_log(void)
{
    (void)fprintf(stderr, "\n");

    vmaf_set_log_level(VMAF_LOG_LEVEL_DEBUG);
    vmaf_log(VMAF_LOG_LEVEL_ERROR, "this is an example %s log\n", "error");
    vmaf_log(VMAF_LOG_LEVEL_WARNING, "this is an example %s log\n", "warning");
    vmaf_log(VMAF_LOG_LEVEL_INFO, "this is an example %s log\n", "info");
    vmaf_log(VMAF_LOG_LEVEL_DEBUG, "this is an example %s log\n", "debug");

    /* NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) — ADR-0141 / ADR-0278 */
    vmaf_log(VMAF_LOG_LEVEL_DEBUG + 1, "this should log nothing\n");
    /* NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) — ADR-0141 / ADR-0278 */
    vmaf_log(VMAF_LOG_LEVEL_NONE - 1, "this should log nothing\n");

    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_vmaf_log);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
