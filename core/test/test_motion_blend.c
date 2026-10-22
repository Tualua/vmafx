/**
 *
 *  Copyright 2016-2023 Netflix, Inc.
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

/* Port of Netflix/vmaf 8cdd55a03 ("libvmaf/test: add test_motion_blend"):
 * motion_blend() of the motion extractors' motion_blend_factor /
 * motion_blend_offset options. The helper is `static` here (fork-local lint
 * convention, as in test_barten_csf.c); upstream's has external linkage. */

#include "test.h"
#include "feature/motion_blend_tools.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy also proposes the `nullptr` keyword, but this is a C
 * translation unit whose sources spell the null pointer constant `NULL` and
 * MSVC's documented /std:clatest C23 feature set does not include `nullptr`
 * while the required Windows build compiles this TU with cl.exe. ADR-1138. */

#define EPS 0.00001

static int almost_equal(double a, double b)
{
    double diff = a > b ? a - b : b - a;
    return diff < EPS;
}

static char *test_motion_blend(void)
{
    mu_assert("motion blend", almost_equal(motion_blend(50.0, 0.5, 40.0), 45.0));
    mu_assert("motion blend offset higher", almost_equal(motion_blend(40.0, 0.5, 50.0), 40.0));
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_motion_blend);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
