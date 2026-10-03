/**
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: EUPL-1.2
 *
 *  float_psnr.c's sum of squared differences, for the host side of a GPU twin
 *  that reads back the exact integer sums of row segments (ADR-1499). One
 *  definition for the CUDA, SYCL and HIP twins. Plain C that is also valid
 *  C++.
 *
 *  extract() adds the value of each row's noise_line() into one double, row
 *  after row. In units of 1 / scaler^2 every term of a row is an integer
 *  below 2^32 (the float square of the sample difference) and a row has at
 *  most 2^15 of them, so every partial sum inside a row is an integer below
 *  2^47 and exact in a double, in the order of any noise_line() variant: the
 *  value of a row is its exact integer sum. The adds of the rows round once
 *  the running sum passes 2^53 units, and only there does the order matter.
 *  So the CPU's sum is the exact integer sum of each row, added row after row
 *  into a double, and a power-of-two scale (1 / scaler^2) does not change
 *  where a double rounds.
 */

#ifndef VMAF_FEATURE_FLOAT_PSNR_ROWS_H_
#define VMAF_FEATURE_FLOAT_PSNR_ROWS_H_

#include <stddef.h>
#include <stdint.h>

/* extract()'s `noise_` before its division by w * h, in units of
 * 1 / scaler^2, from `rows` rows of `per_row` segment sums each (row-major):
 * each row's segments added exactly in 64 bits, then the rows added into one
 * double in order. */
static inline double vmaf_float_psnr_row_noise(const uint64_t *segments, unsigned rows,
                                               unsigned per_row)
{
    double noise = 0.0;
    for (unsigned y = 0u; y < rows; y++) {
        uint64_t row = 0u;
        for (unsigned x = 0u; x < per_row; x++)
            row += segments[((size_t)y * per_row) + x];
        noise += (double)row;
    }
    return noise;
}

#endif /* VMAF_FEATURE_FLOAT_PSNR_ROWS_H_ */
