/**
 *
 *  Copyright 2016-2020 Netflix, Inc.
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
#include "feature/adm_csf_tools.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy also proposes the `nullptr` keyword, but MSVC's
 * documented /std:clatest C23 feature set does not include `nullptr`. This
 * test follows the cross-platform spelling of the surface it exercises.
 * ADR-1138. */

#define EPS 0.00001

/* Test support function. `static` (vs. upstream's external linkage) is a
 * fork-local lint-cleanup per CLAUDE.md §12 r12; the helper is only used
 * inside this translation unit. Matches the convention already used in
 * upstream's own test_vif_tools.c. */
static int almost_equal(double a, double b)
{
    double diff = a > b ? a - b : b - a;
    return diff < EPS;
}

static char *test_adm_csf(void)
{
    mu_assert("adm csf mismatch", almost_equal(adm_native_csf(3, 3.0, 1080, 0), 0.986264592442799));
    mu_assert("adm csf mismatch",
              almost_equal(adm_native_csf(3, 3.0, 1080, 45), 0.8773599546532113));
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_adm_csf);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
