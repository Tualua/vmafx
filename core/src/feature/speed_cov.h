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

#ifndef FEATURE_SPEED_COV_H_
#define FEATURE_SPEED_COV_H_

#include <stddef.h>

/*
 * Covariance sums of SpEED's 25x25 covariance matrix
 * (`compute_covariance_matrix` in speed.c), ADR-1459.
 *
 * Contract -- every implementation returns the bits of the reference:
 *
 *   sum = 0
 *   for i in [0, height), j in [0, width):       (row-major, this order)
 *       sum = sum + (((double)x[i][j] - mean_x) * ((double)y[i][j] - mean_y))
 *
 * one binary64 subtraction per operand, one multiplication and one addition
 * per element, each rounded on its own. A fused multiply-add rounds once and
 * breaks the contract; so does any other order of the additions, which is
 * why a kernel that splits one sum over vector lanes cannot be used here.
 *
 * What vectorises is the set of sums. Block x is paired with the `count`
 * blocks y_0 .. y_{count-1} that start at consecutive columns of one row,
 * y_k[i][j] = data_y[i * stride_px + j + k]. A row kernel keeps one lane per
 * y_k: every lane holds its own running sum and adds its own product, in the
 * reference's order. The lane index is an output index, not a reduction
 * axis, the property `speed_matmul.h` relies on too.
 *
 * `data_y` must have `width + SPEED_COV_ROW_MAX - 1` readable floats in each
 * of its `height` rows whatever `count` is: a kernel may load all
 * SPEED_COV_ROW_MAX blocks and discard the lanes at and above `count`. SpEED
 * always has them (the block grid is SPEED_COV_ROW_MAX columns wider than a
 * block). `sums` receives `count` values; nothing is written past them.
 *
 * Strides are element counts (floats), not bytes. 1 <= count <=
 * SPEED_COV_ROW_MAX.
 */

/* DEFAULT_BLOCK_SIZE of speed.c: the y blocks of one row of the block grid. */
#define SPEED_COV_ROW_MAX 5

typedef void (*speed_cov_row_fn)(const float *data_x, const float *data_y, size_t stride_px,
                                 size_t height, size_t width, double mean_x, const double *mean_y,
                                 size_t count, double *sums);

/* The reference: one sum (speed.c). */
double compute_cov_kernel_scalar(const float *data_x, const float *data_y, size_t stride_px,
                                 size_t height, size_t width, double mean_x, double mean_y);

/* Portable row kernel: `count` calls of the reference (speed.c). */
void speed_cov_row_scalar(const float *data_x, const float *data_y, size_t stride_px, size_t height,
                          size_t width, double mean_x, const double *mean_y, size_t count,
                          double *sums);

#endif /* FEATURE_SPEED_COV_H_ */
