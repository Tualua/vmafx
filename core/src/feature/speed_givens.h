/**
 *  Copyright 2016-2025 Netflix, Inc.
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 *  The one fp64 statement of speed.c's create_givens(), for a device kernel
 *  that has no fp64 type (ADR-1477).
 *
 *  Netflix's create_givens() (libvmaf/src/feature/speed.c) stores
 *
 *      float s1 = 1.0 / sqrt(1 + t * t);
 *
 *  `1 + t * t` is an fp32 value u; the square root and the quotient are fp64
 *  operations, and the assignment rounds the fp64 quotient to fp32. That is
 *  three roundings, and it is not `1.0f / sqrtf(u)`: the two differ on
 *  2,907,055 of the 8,388,609 values u can take.
 *
 *  speed_givens_unit(u) returns the reference's float from fp32 operations
 *  alone: the correctly rounded square root r and reciprocal q, their two
 *  residuals (each exact in one fused multiply-add), and one correction of q
 *  added with a single rounding. It costs the square root and the division
 *  the fp32 form has, plus two fused multiply-adds, four products and two
 *  sums; the corrections divide by nothing.
 *
 *  Domain and proof. create_givens() forms t as the quotient of the smaller
 *  magnitude by the larger, so |t| <= 1 and u = 1 + t * t lies in [1, 2], or
 *  is NaN when an operand was. [1, 2] holds 2^23 + 1 floats.
 *  core/test/test_speed_upstream_form.c evaluates this function and the reference's
 *  statement on every one of them and requires equal bits; there is no
 *  tolerance and no table of exceptions. A NaN stays a NaN.
 *
 *  The caller names its correctly rounded fp32 primitives before including
 *  the header; each must round once and none may be contracted with a
 *  neighbour:
 *      SPEED_GIVENS_FUNC       function qualifiers
 *      SPEED_GIVENS_SQRT(x)    square root
 *      SPEED_GIVENS_DIV(a, b)  quotient
 *      SPEED_GIVENS_MUL(a, b)  product
 *      SPEED_GIVENS_SUB(a, b)  difference
 *      SPEED_GIVENS_ADD(a, b)  sum
 *      SPEED_GIVENS_FMA(a, b, c)  a * b + c rounded once
 *  Without them the header uses the C operators and <math.h>, which round
 *  once in a translation unit built with contraction off (every C and C++
 *  translation unit of the library, ADR-1461).
 */

#ifndef VMAF_SRC_FEATURE_SPEED_GIVENS_H_
#define VMAF_SRC_FEATURE_SPEED_GIVENS_H_

#ifndef SPEED_GIVENS_FUNC
#include <math.h>
#define SPEED_GIVENS_FUNC static inline
#define SPEED_GIVENS_SQRT(x) sqrtf(x)
#define SPEED_GIVENS_DIV(a, b) ((a) / (b))
#define SPEED_GIVENS_MUL(a, b) ((a) * (b))
#define SPEED_GIVENS_SUB(a, b) ((a) - (b))
#define SPEED_GIVENS_ADD(a, b) ((a) + (b))
#define SPEED_GIVENS_FMA(a, b, c) fmaf((a), (b), (c))
#endif

/* `(float)(1.0 / sqrt((double)u))` for u in [1, 2]. */
SPEED_GIVENS_FUNC float speed_givens_unit(float u)
{
    const float root = SPEED_GIVENS_SQRT(u);
    const float root_residual = SPEED_GIVENS_FMA(-root, root, u); /* u - root^2, exact */
    const float unit = SPEED_GIVENS_DIV(1.0f, root);
    const float unit_residual = SPEED_GIVENS_FMA(-unit, root, 1.0f); /* 1 - unit * root, exact */
    /* sqrt(u) = root + root_shift to first order: root_residual / (2 * root),
     * with `unit` standing in for 1 / root. Both corrections are 2^-24 of
     * their term, so that substitution costs 2^-48 of the result. */
    const float root_shift = SPEED_GIVENS_MUL(root_residual, SPEED_GIVENS_MUL(0.5f, unit));
    const float carried = SPEED_GIVENS_MUL(unit, root_shift);
    const float correction = SPEED_GIVENS_MUL(SPEED_GIVENS_SUB(unit_residual, carried), unit);
    return SPEED_GIVENS_ADD(unit, correction);
}

#endif /* VMAF_SRC_FEATURE_SPEED_GIVENS_H_ */
