/**
 *  Copyright (c) the JPEG XL Project Authors.
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: BSD-2-Clause-Patent AND BSD-3-Clause
 *
 *  The per-pixel terms of ssimulacra2.c::ssim_map() and ::edge_diff_map() as
 *  the CPU computes them in fp64, for the SYCL twin (ADR-1446). The
 *  reference's lines:
 *
 *      double d = 1.0 - ((double)num_m * (double)num_s / (double)denom_s);
 *      if (d < 0.0)
 *          d = 0.0;
 *      sum_l1 += d;
 *      sum_l4 += quartic(d);
 *
 *      double ed1 = fabs((double)r1[i] - (double)rm1[i]);
 *      double ed2 = fabs((double)r2[i] - (double)rm2[i]);
 *      double d1 = (1.0 + ed2) / (1.0 + ed1) - 1.0;
 *      vmaf_ss2_split_edge_difference(d1, &art, &det);
 *      s0 += art;
 *      s1 += quartic(art);
 *      s2 += det;
 *      s3 += quartic(det);
 *
 *  with quartic(x) = (x * x) * (x * x). A SYCL kernel has no fp64 type
 *  (ADR-0220), and each of these terms goes into a sum of doubles whose bits
 *  the twin has to return, so the term has to be the CPU's double and not a
 *  value near it. The functions here run the reference's operations, one for
 *  one, on fp64 values held in 64-bit integers (sycl_soft_signed.h) and
 *  return fp64 bit patterns.
 *
 *  Range. For finite inputs a term is the reference's double, and its
 *  fourth power is while the term is below 2^250: a zero denominator
 *  included, where the reference's quotient is an infinity of the product's
 *  sign and d is 0 or infinite. A value the reference computes as an
 *  infinity or a NaN (a non-finite input, an infinite d, the fourth power of
 *  a term of 2^250 or more) is returned as a NaN. The sum keeps it and the
 *  frame guard rejects the frame, as it does on the CPU for an infinite or
 *  NaN sum. The blurred planes of finite pictures are finite and their terms
 *  are far below that.
 *
 *  Kernel-safe: see sycl_soft_signed.h.
 */

#ifndef VMAF_FEATURE_SYCL_SYCL_SSIMULACRA2_MATH_H_
#define VMAF_FEATURE_SYCL_SYCL_SSIMULACRA2_MATH_H_

#include <sycl/sycl.hpp>

#include <cstdint>

#include "sycl_soft_signed.h"

namespace vmaf_sycl_ss2
{

using vmaf_sycl_soft::signed_abs;
using vmaf_sycl_soft::signed_add;
using vmaf_sycl_soft::signed_bits;
using vmaf_sycl_soft::signed_div;
using vmaf_sycl_soft::signed_from_float;
using vmaf_sycl_soft::signed_make;
using vmaf_sycl_soft::signed_mul;
using vmaf_sycl_soft::signed_sub;
using vmaf_sycl_soft::SoftSigned;

/* 1.0 */
inline constexpr SoftSigned kOne = {.mant = vmaf_sycl_soft::kDoubleTop, .exp = -52, .negative = 0u};
/* The quiet NaN a term of a non-finite input is returned as. */
inline constexpr uint64_t kNanBits = vmaf_sycl_soft::kQuietNanBits;

/* quartic(): x *= x; return x * x; */
VMAF_SYCL_ALWAYS_INLINE SoftSigned quartic(SoftSigned x)
{
    const SoftSigned square = signed_mul(x, x);
    return signed_mul(square, square);
}

/* A value and its fourth power, as fp64 bit patterns. */
struct TermPair {
    uint64_t value;
    uint64_t fourth;
};

/* The exponent of 2^250 as a SoftSigned's: mant is in [2^52, 2^53). */
inline constexpr int32_t kTermLimitExp = 250 - 52;

/* `x` and quartic(x) for a non-negative term the reference computes as a
 * finite value; a NaN for both otherwise, and for the fourth power alone
 * where only that overflows (see Range above). */
VMAF_SYCL_ALWAYS_INLINE TermPair term_pair(SoftSigned x, bool finite)
{
    const bool fourth_finite = finite && (x.mant == 0u || x.exp < kTermLimitExp);
    return {.value = finite ? signed_bits(x) : kNanBits,
            .fourth = fourth_finite ? signed_bits(quartic(x)) : kNanBits};
}

/* ssim_map(): d and quartic(d) from the three fp32 values the reference
 * forms first (num_m, num_s, denom_s; the caller computes them in fp32 as
 * the reference does). The product of two converted floats is exact in fp64,
 * so the quotient is the first rounding. */
VMAF_SYCL_ALWAYS_INLINE TermPair ssim_terms(float num_m, float num_s, float denom_s)
{
    const SoftSigned product = signed_mul(signed_from_float(num_m), signed_from_float(num_s));
    const SoftSigned ratio = signed_div(product, signed_from_float(denom_s));
    const SoftSigned distance = signed_sub(kOne, ratio);
    /* A zero denominator: the quotient is an infinity of the product's sign
     * (the denominator's sign bit counts), or a NaN for a zero product. A
     * positive infinity makes d negative infinity, which the clamp below
     * turns into zero; the other two are not finite. */
    const bool zero_denominator = denom_s == 0.0f;
    const bool denominator_negative = (sycl::bit_cast<uint32_t>(denom_s) >> 31) != 0u;
    const bool ratio_plus_infinity =
        zero_denominator && product.mant != 0u && (product.negative != 0u) == denominator_negative;
    /* if (d < 0.0) d = 0.0; */
    const bool clamp = ratio_plus_infinity || distance.negative != 0u;
    const SoftSigned clamped =
        signed_make(clamp ? uint64_t{0} : distance.mant, distance.exp, false);
    const bool finite = sycl::isfinite(num_m) && sycl::isfinite(num_s) && sycl::isfinite(denom_s) &&
                        (!zero_denominator || ratio_plus_infinity);
    return term_pair(clamped, finite);
}

/* The four edge terms of one pixel: artifact, quartic(artifact), detail,
 * quartic(detail). */
struct EdgeTerms {
    TermPair artifact;
    TermPair detail;
};

/* fabs((double)a - (double)b) */
VMAF_SYCL_ALWAYS_INLINE SoftSigned abs_difference(float a, float b)
{
    return signed_abs(signed_sub(signed_from_float(a), signed_from_float(b)));
}

/* edge_diff_map(): d1 = (1.0 + ed2) / (1.0 + ed1) - 1.0, split by
 * vmaf_ss2_split_edge_difference() into its positive part (artifact) and the
 * negation of its negative part (detail). */
VMAF_SYCL_ALWAYS_INLINE EdgeTerms edge_terms(float r1, float m1, float r2, float m2)
{
    const SoftSigned numerator = signed_add(kOne, abs_difference(r2, m2));
    const SoftSigned denominator = signed_add(kOne, abs_difference(r1, m1));
    const SoftSigned difference = signed_sub(signed_div(numerator, denominator), kOne);
    const bool negative = difference.negative != 0u;
    const SoftSigned artifact =
        signed_make(negative ? uint64_t{0} : difference.mant, difference.exp, false);
    const SoftSigned detail =
        signed_make(negative ? difference.mant : uint64_t{0}, difference.exp, false);
    const bool finite =
        sycl::isfinite(r1) && sycl::isfinite(m1) && sycl::isfinite(r2) && sycl::isfinite(m2);
    return {.artifact = term_pair(artifact, finite), .detail = term_pair(detail, finite)};
}

} // namespace vmaf_sycl_ss2

#endif /* VMAF_FEATURE_SYCL_SYCL_SSIMULACRA2_MATH_H_ */
