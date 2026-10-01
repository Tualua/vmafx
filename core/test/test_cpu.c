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

#include "test.h"

#include "cpu.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy also proposes the `nullptr` keyword, but MSVC's
 * documented /std:clatest C23 feature set does not include `nullptr`. This
 * test follows the cross-platform spelling of the surface it exercises.
 * ADR-1138. */

static char *test_cpu(void)
{
    unsigned flags = 0;

    flags = vmaf_get_cpu_flags();
    mu_assert("flags should be zero before vmaf_init_cpu()", !flags);
    vmaf_init_cpu();

    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_cpu);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
