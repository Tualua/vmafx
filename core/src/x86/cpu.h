/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/**
 *
 *  Copyright 2016-2026 Netflix, Inc.
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

#ifndef VMAF_SRC_X86_CPU_H_
#define VMAF_SRC_X86_CPU_H_

/* NOLINTBEGIN(performance-enum-size): C header included by C and C++ translation units; C has no fixed enum underlying type across the required toolchains (ADR-1470). ADR-1138. */
enum VmafCpuFlags {
    VMAF_X86_CPU_FLAG_SSE2 = 1 << 0,
    VMAF_X86_CPU_FLAG_SSSE3 = 1 << 1,
    VMAF_X86_CPU_FLAG_SSE41 = 1 << 2,
    VMAF_X86_CPU_FLAG_AVX2 = 1 << 3,
    VMAF_X86_CPU_FLAG_AVX512 = 1 << 4,
    VMAF_X86_CPU_FLAG_AVX512ICL = 1 << 5,
};
/* NOLINTEND(performance-enum-size) */

unsigned vmaf_get_cpu_flags_x86(void);

#endif /* VMAF_SRC_X86_CPU_H_ */
