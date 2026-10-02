/**
 *
 *  Copyright 2016-2025 Netflix, Inc.
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

#include <arm_neon.h>
#include <assert.h>
#include <stddef.h>

#include "feature/speed_cov.h"
#include "speed_neon.h"

/* One x block against up to five y blocks at consecutive columns
 * (speed_cov.h, ADR-1459). Two 2-lane registers hold the running sums of
 * y_0..y_3 and a scalar the sum of y_4. Each lane multiplies (vmulq_f64) and
 * then adds (vaddq_f64), as compute_cov_kernel_scalar() does for that pair:
 * no vfmaq_f64, and no lane ever feeds another. This is not the kernel of
 * Netflix/vmaf 15297286, which splits one sum over eight lanes and so cannot
 * return the reference's bits. The lanes at and above `count` are computed
 * from readable floats and dropped. */
void speed_cov_row_neon(const float *data_x, const float *data_y, size_t stride_px, size_t height,
                        size_t width, double mean_x, const double *mean_y, size_t count,
                        double *sums)
{
    assert(count >= 1 && count <= SPEED_COV_ROW_MAX);
    double lanes[SPEED_COV_ROW_MAX + 1] = {0.0};
    for (size_t k = 0; k < count; k++)
        lanes[k] = mean_y[k];

    const float64x2_t my01 = vld1q_f64(lanes);
    const float64x2_t my23 = vld1q_f64(lanes + 2);
    const double my4 = lanes[4];
    float64x2_t acc01 = vdupq_n_f64(0.0);
    float64x2_t acc23 = vdupq_n_f64(0.0);
    double acc4 = 0.0;

    for (size_t i = 0; i < height; i++) {
        const float *row_x = data_x + i * stride_px;
        const float *row_y = data_y + i * stride_px;

        for (size_t j = 0; j < width; j++) {
            const double dx_scalar = (double)row_x[j] - mean_x;
            const float64x2_t dx = vdupq_n_f64(dx_scalar);
            const float32x4_t fy = vld1q_f32(row_y + j);
            const float64x2_t dy01 = vsubq_f64(vcvt_f64_f32(vget_low_f32(fy)), my01);
            const float64x2_t dy23 = vsubq_f64(vcvt_high_f64_f32(fy), my23);
            acc01 = vaddq_f64(acc01, vmulq_f64(dx, dy01));
            acc23 = vaddq_f64(acc23, vmulq_f64(dx, dy23));
            /* The product is its own statement and this TU is built without
             * contraction (arm64_v8_fp): a fused multiply-add here would
             * round once where the reference rounds twice. */
            const double dy4 = (double)row_y[j + 4] - my4;
            const double product4 = dx_scalar * dy4;
            acc4 += product4;
        }
    }

    vst1q_f64(lanes, acc01);
    vst1q_f64(lanes + 2, acc23);
    lanes[4] = acc4;
    for (size_t k = 0; k < count; k++)
        sums[k] = lanes[k];
}
