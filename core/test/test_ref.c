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
#include "ref.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy also proposes the `nullptr` keyword, but MSVC's
 * documented /std:clatest C23 feature set does not include `nullptr`. This
 * test follows the cross-platform spelling of the surface it exercises.
 * ADR-1138. */

static char *test_ref_init_inc_dec_close(void)
{
    int err = 0;
    long val = 0;
    VmafRef *ref;

    err = vmaf_ref_init(&ref);
    mu_assert("problem during vmaf_ref_init", !err);
    val = vmaf_ref_load(ref);
    mu_assert("initial value should be 1", val == 1);
    vmaf_ref_fetch_increment(ref);
    val = vmaf_ref_load(ref);
    mu_assert("value should be incremented to 2", val == 2);
    vmaf_ref_fetch_decrement(ref);
    val = vmaf_ref_load(ref);
    mu_assert("value should be decremented to 1", val == 1);
    err = vmaf_ref_close(ref);
    mu_assert("problem during vmaf_ref_close", !err);

    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_ref_init_inc_dec_close);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
