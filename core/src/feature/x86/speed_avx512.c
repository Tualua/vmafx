/**
 *
 *  Copyright 2016-2020 Netflix, Inc.
 *  Copyright 2026 Lusoris
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

#include <assert.h>
#include <immintrin.h>
#include <stddef.h>

#include "feature/speed_cov.h"
#include "speed_avx512.h"

/* One x block against up to five y blocks at consecutive columns
 * (speed_cov.h, ADR-1459). Lane k of a 512-bit register holds the running sum
 * of y_k; each lane multiplies and then adds, as compute_cov_kernel_scalar()
 * does for that pair: no FMA, and no lane ever feeds another. The masked load
 * reads the five floats the contract guarantees and no more. */
void speed_cov_row_avx512(const float *data_x, const float *data_y, size_t stride_px, size_t height,
                          size_t width, double mean_x, const double *mean_y, size_t count,
                          double *sums)
{
    assert(count >= 1 && count <= SPEED_COV_ROW_MAX);
    const __mmask8 readable = (__mmask8)((1u << SPEED_COV_ROW_MAX) - 1u);
    const __mmask8 wanted = (__mmask8)((1u << count) - 1u);
    const __m512d mx = _mm512_set1_pd(mean_x);
    const __m512d my = _mm512_maskz_loadu_pd(wanted, mean_y);
    __m512d acc = _mm512_setzero_pd();

    for (size_t i = 0; i < height; i++) {
        const float *row_x = data_x + i * stride_px;
        const float *row_y = data_y + i * stride_px;

        for (size_t j = 0; j < width; j++) {
            const __m512d dx = _mm512_sub_pd(_mm512_cvtps_pd(_mm256_broadcast_ss(row_x + j)), mx);
            const __m256 fy = _mm256_maskz_loadu_ps(readable, row_y + j);
            const __m512d dy = _mm512_sub_pd(_mm512_cvtps_pd(fy), my);
            acc = _mm512_add_pd(acc, _mm512_mul_pd(dx, dy));
        }
    }

    _mm512_mask_storeu_pd(sums, wanted, acc);
}
