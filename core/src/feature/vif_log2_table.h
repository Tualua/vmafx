/**
 *
 *  Copyright 2016-2026 Netflix, Inc.
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

#ifndef FEATURE_VIF_LOG2_TABLE_H_
#define FEATURE_VIF_LOG2_TABLE_H_

/*
 * The log2 table of the fixed-point VIF extractor, and its one definition.
 *
 * Plain C that is also valid C++ and Objective-C++, with no include beyond
 * the C library: integer_vif.h includes it for the CPU extractor, and the
 * hosts of vif_hip, vif_sycl and vif_metal include it to upload the CPU's
 * values. A device's log2f() need not round as the host's does (ADR-1435:
 * 77 entries differ on a gfx1036), so a twin either reads this table or
 * proves every entry: vif_cuda evaluates the expression on the device, and
 * test_cuda_vif_log2_table compares all VIF_LOG2_TABLE_SIZE entries with this
 * table (ADR-1456).
 */

#include <math.h>
#include <stdint.h>

/*
 * log2 LUT shrink (ADR-0500): after the clz-based normalisation in log2_32 / log2_64
 * the mantissa is always in [32768..65535] — bit 15 is always 1.  Stripping bit 15
 * (masking with 0x7FFF) gives a 15-bit index in [0..32767], so the table needs only
 * 32768 entries (64 KB) instead of the original 128 KB.  This halves L2 pressure and
 * TLB coverage on the hot vif_statistic_avx512 gather path.
 * Bit-exactness is preserved: same uint16 values, same arithmetic.
 */
#define VIF_LOG2_TABLE_SIZE 32768u
#define VIF_LOG2_TABLE_OFFSET 0x8000u

/*
 * Fill the VIF_LOG2_TABLE_SIZE entries of the log2 table: entry i is
 * log2(VIF_LOG2_TABLE_OFFSET + i) * 2048 rounded to an integer, from the host
 * math library's log2f() and roundf(). log2_32 / log2_64 mask the normalised
 * mantissa with VIF_LOG2_TABLE_SIZE - 1 to recover i; the AVX-512 gather path
 * applies the same mask before the gather (ADR-0500). roundf() of a float
 * below 2^15 is the same integer as round() of its double promotion.
 */
static inline void vif_log2_table_generate(uint16_t *log2_table)
{
    for (unsigned i = 0; i < VIF_LOG2_TABLE_SIZE; ++i) {
        log2_table[i] = (uint16_t)roundf(log2f((float)(VIF_LOG2_TABLE_OFFSET + i)) * 2048);
    }
}

#endif /* FEATURE_VIF_LOG2_TABLE_H_ */
