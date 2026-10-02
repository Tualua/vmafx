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
#include "speed_avx2.h"

/* One x block against up to five y blocks at consecutive columns
 * (speed_cov.h, ADR-1459). Lanes 0..3 of a 256-bit register hold the running
 * sums of y_0..y_3 and the low lane of a 128-bit register the sum of y_4.
 * Each lane multiplies and then adds, as compute_cov_kernel_scalar() does for
 * that pair: no FMA, and no lane ever feeds another. The lanes at and above
 * `count` are computed from readable floats and dropped. */
void speed_cov_row_avx2(const float *data_x, const float *data_y, size_t stride_px, size_t height,
                        size_t width, double mean_x, const double *mean_y, size_t count,
                        double *sums)
{
    assert(count >= 1 && count <= SPEED_COV_ROW_MAX);
    double lanes[SPEED_COV_ROW_MAX] = {0.0};
    for (size_t k = 0; k < count; k++)
        lanes[k] = mean_y[k];

    const __m256d mx = _mm256_set1_pd(mean_x);
    const __m256d my_lo = _mm256_loadu_pd(lanes);
    const __m128d my_hi = _mm_set_sd(lanes[4]);
    __m256d acc_lo = _mm256_setzero_pd();
    __m128d acc_hi = _mm_setzero_pd();

    for (size_t i = 0; i < height; i++) {
        const float *row_x = data_x + i * stride_px;
        const float *row_y = data_y + i * stride_px;

        for (size_t j = 0; j < width; j++) {
            const __m256d dx = _mm256_sub_pd(_mm256_cvtps_pd(_mm_broadcast_ss(row_x + j)), mx);
            const __m256d dy_lo = _mm256_sub_pd(_mm256_cvtps_pd(_mm_loadu_ps(row_y + j)), my_lo);
            const __m128d dy_hi = _mm_sub_sd(_mm_cvtps_pd(_mm_load_ss(row_y + j + 4)), my_hi);
            acc_lo = _mm256_add_pd(acc_lo, _mm256_mul_pd(dx, dy_lo));
            acc_hi = _mm_add_sd(acc_hi, _mm_mul_sd(_mm256_castpd256_pd128(dx), dy_hi));
        }
    }

    _mm256_storeu_pd(lanes, acc_lo);
    _mm_store_sd(lanes + 4, acc_hi);
    for (size_t k = 0; k < count; k++)
        sums[k] = lanes[k];
}
