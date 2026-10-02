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

#if ARCH_X86
#include "x86/avx512_warm_up.h"
#endif

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

#if ARCH_X86
static double tripled(double x)
{
    return x * 3.0;
}
#endif

/* The AVX-512 warm-up writes zmm0 and has to say so in a form every compiler
 * honours. clang drops a "zmm0" clobber in a function not compiled for
 * AVX-512, and then keeps `scaled` below in xmm0 across the statement, which
 * zeroes it: the sum comes out as `offset`. A link-time-optimised clang build
 * inlined vmaf_init_cpu() into a caller that way and test_ciede_device_math
 * computed 45 - 20 * log10(x) as 45.
 *
 * The call through a volatile pointer returns in xmm0 and cannot be folded;
 * the product stays there. Hosts without AVX-512 never run the statement. */
static char *test_avx512_warm_up_keeps_xmm0(void)
{
#if ARCH_X86
    if (!(vmaf_get_cpu_flags() & VMAF_X86_CPU_FLAG_AVX512))
        return NULL;

    double (*volatile source)(double) = tripled;
    volatile double input = 0.4791505;
    const double gain = -20.0;
    const double offset = 45.0;

    const double expected = source(input) * gain + offset;
    const double scaled = source(input) * gain;
    vmaf_x86_avx512_warm_up();
    const double after = scaled + offset;

    mu_assert("the AVX-512 warm-up changed a value its caller holds in xmm0", after == expected);
#endif
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_cpu);
    mu_run_test(test_avx512_warm_up_keeps_xmm0);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
