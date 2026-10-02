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

#ifndef ARM64_SPEED_NEON_H_
#define ARM64_SPEED_NEON_H_

#include <stddef.h>

/* speed_cov_row_fn for NEON: see ../speed_cov.h for the contract. */
void speed_cov_row_neon(const float *data_x, const float *data_y, size_t stride_px, size_t height,
                        size_t width, double mean_x, const double *mean_y, size_t count,
                        double *sums);

#endif /* ARM64_SPEED_NEON_H_ */
