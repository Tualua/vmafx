/**
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: EUPL-1.2
 *
 *  One entry of SpEED's 25x25 covariance matrix as speed.c computes it, for
 *  the SYCL twin, which has no fp64 type (ADR-0220).
 *
 *  speed.c::compute_covariance_row() takes the entry from
 *  compute_cov_kernel_scalar(), which adds `(val_x - mean_x) * (val_y -
 *  mean_y)` into one fp64 running sum, row after row and column after column,
 *  and stores `(float)(sum / (width * height))`. Every fp64 add rounds, and
 *  where it rounds depends on the running sum, so the reference's sum is not
 *  the exact sum of its terms: on a covariance whose terms cancel (an
 *  off-diagonal entry of a textured plane) the two differ by about 1e-14 of
 *  the sum, and now and then that moves the stored fp32 value by one step.
 *  The twin used to add exact fp32-pair products in parallel and round the
 *  near-exact total once; frame 140 of a 3840x1600 10-bit segment gave one
 *  entry the neighbouring fp32 value and speed_chroma_u one step off the
 *  CPU's score.
 *
 *  covariance_entry() performs the reference's operations in its order, each
 *  one rounded as the fp64 operation it stands for (sycl_soft_signed.h): the
 *  two differences, the product, the add, then the quotient by the term count
 *  and its conversion to fp32. The result is the CPU's bits, on the host and
 *  on the device. The loop is sequential by construction; a kernel runs one
 *  work-item per entry.
 *
 *  Range: the plane and the means are finite fp32 values, so every difference
 *  and product is a normal fp64 value or zero (sycl_soft_signed.h's range).
 *  A zero has no sign there; the reference's sum starts at +0, so a zero sum
 *  is +0 on both sides.
 *
 *  Kernel-safe: integers and fp32 only, no arrays, every helper always
 *  inlined (ADR-1395).
 */

#ifndef VMAF_FEATURE_SYCL_SYCL_SPEED_COV_MATH_H_
#define VMAF_FEATURE_SYCL_SYCL_SPEED_COV_MATH_H_

#include <sycl/sycl.hpp>

#include <cstddef>
#include <cstdint>

#include "sycl_compat.h"
#include "sycl_soft_double.h"
#include "sycl_soft_signed.h"

namespace vmaf_sycl_speed_cov
{

/* The fp32 value of an fp64 value: ties to even, a subnormal result or zero
 * below the normal range. */
VMAF_SYCL_ALWAYS_INLINE float soft_signed_to_float(vmaf_sycl_soft::SoftSigned value)
{
    const float magnitude =
        vmaf_sycl_soft::soft_to_float_any({.mant = value.mant, .exp = value.exp});
    return value.negative != 0u ? -magnitude : magnitude;
}

/* compute_cov_kernel_scalar() divided by the term count and stored as fp32,
 * as compute_covariance_row() stores it. `x` and `y` point at the first
 * sample of the two submatrices, `stride` is the plane's row pitch in
 * samples, `width` x `height` the submatrix. */
VMAF_SYCL_ALWAYS_INLINE float covariance_entry(const float *x, const float *y, size_t stride,
                                               uint32_t width, uint32_t height, float mean_x,
                                               float mean_y)
{
    using vmaf_sycl_soft::signed_add;
    using vmaf_sycl_soft::signed_from_float;
    using vmaf_sycl_soft::signed_mul;
    using vmaf_sycl_soft::signed_sub;
    using vmaf_sycl_soft::SoftSigned;

    const SoftSigned mx = signed_from_float(mean_x);
    const SoftSigned my = signed_from_float(mean_y);
    SoftSigned sum = vmaf_sycl_soft::signed_make(0u, 0, false);
    for (uint32_t i = 0; i < height; i++) {
        const float *row_x = x + static_cast<size_t>(i) * stride;
        const float *row_y = y + static_cast<size_t>(i) * stride;
        for (uint32_t j = 0; j < width; j++) {
            const SoftSigned dx = signed_sub(signed_from_float(row_x[j]), mx);
            const SoftSigned dy = signed_sub(signed_from_float(row_y[j]), my);
            sum = signed_add(sum, signed_mul(dx, dy));
        }
    }
    const uint64_t count = static_cast<uint64_t>(width) * height;
    const SoftSigned quotient =
        vmaf_sycl_soft::signed_div(sum, vmaf_sycl_soft::signed_from_exact(count));
    return soft_signed_to_float(quotient);
}

} // namespace vmaf_sycl_speed_cov

#endif /* VMAF_FEATURE_SYCL_SYCL_SPEED_COV_MATH_H_ */
