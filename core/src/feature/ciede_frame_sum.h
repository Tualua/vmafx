/**
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: EUPL-1.2
 *
 *  The frame sum of ciede.c's extract(), for the host side of a GPU twin
 *  that reads one float per pixel back from the device (ADR-1426, ADR-1436,
 *  ADR-1448). The reference adds every pixel's value into one double in
 *  raster order; a sum in another order rounds differently (1.4e-9 of the
 *  score at 3840x2160 for per-block sums). One definition for every twin.
 *  Plain C that is also valid C++.
 */

#ifndef VMAF_FEATURE_CIEDE_FRAME_SUM_H_
#define VMAF_FEATURE_CIEDE_FRAME_SUM_H_

#include <stddef.h>

/* extract()'s `de00_sum`: every pixel's value added into one double, row
 * after row, each row left to right. */
static inline double ciede_frame_sum(const float *terms, size_t count)
{
    double de00_sum = 0.0;
    for (size_t i = 0u; i < count; i++)
        de00_sum += (double)terms[i];
    return de00_sum;
}

#endif /* VMAF_FEATURE_CIEDE_FRAME_SUM_H_ */
