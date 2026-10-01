/**
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: EUPL-1.2
 *
 *  The per-pixel term of the fixed-point SSIM extractor, as
 *  integer_ssim.c::ssim_reduce_row_range() computes it in fp64, for the SYCL
 *  twin (ADR-1443). The reference's lines:
 *
 *      w_d = m.w;
 *      c1 = sm * sm * SSIM_K1 * w_d * w_d;
 *      c2 = sm * sm * SSIM_K2 * w_d * w_d;
 *      mx2 = m.mux * (double)m.mux;
 *      mxy = m.mux * (double)m.muy;
 *      my2 = m.muy * (double)m.muy;
 *      *ssim += m.w * (2 * mxy + c1) * (c2 + 2 * (m.xy * w_d - mxy)) /
 *               ((mx2 + my2 + c1) * (m.x2 * w_d - mx2 + m.y2 * w_d - my2 + c2));
 *
 *  A SYCL kernel has no fp64 type (ADR-0220), and the frame sum the term
 *  goes into is a sum of doubles, so the term has to be that double and not
 *  a value near it. term_bits() therefore runs the reference's operations,
 *  one for one and in the reference's order, on fp64 values held in 64-bit
 *  integers (sycl_soft_signed.h), and returns the term's fp64 bit pattern.
 *
 *  The moments are integers and non-negative. A product of two of them is
 *  exact in 64 bits at every bit depth (mux, muy < 2^32; x2, xy, y2 < 2^48;
 *  w <= 2^16), so each of the reference's products of two converted integers
 *  is one rounding of an exact integer, signed_from_u64().
 *
 *  Kernel-safe: see sycl_soft_signed.h.
 */

#ifndef VMAF_FEATURE_SYCL_SYCL_INTEGER_SSIM_MATH_H_
#define VMAF_FEATURE_SYCL_SYCL_INTEGER_SSIM_MATH_H_

#include <sycl/sycl.hpp>

#include <bit>
#include <cstdint>

#include "sycl_soft_signed.h"

namespace vmaf_sycl_issim
{

using vmaf_sycl_soft::signed_add;
using vmaf_sycl_soft::signed_bits;
using vmaf_sycl_soft::signed_div;
using vmaf_sycl_soft::signed_from_bits;
using vmaf_sycl_soft::signed_from_u64;
using vmaf_sycl_soft::signed_mul;
using vmaf_sycl_soft::signed_sub;
using vmaf_sycl_soft::signed_twice;
using vmaf_sycl_soft::SoftSigned;

/* integer_ssim.c's SSIM_K1 and SSIM_K2. */
inline constexpr double kSsimK1 = 0.01 * 0.01;
inline constexpr double kSsimK2 = 0.03 * 0.03;

/* One pixel's window moments: integer_ssim.c's ssim_moments. */
struct Moments {
    uint64_t mux;
    uint64_t muy;
    uint64_t x2;
    uint64_t xy;
    uint64_t y2;
    uint64_t w;
};

/* The part of the reference's c1 and c2 that is the same for every pixel:
 * fl64(sm * sm * SSIM_K1) and fl64(sm * sm * SSIM_K2). */
struct Stabilisers {
    SoftSigned k1;
    SoftSigned k2;
};

/* Host only: the stabilisers of a bit depth, from the reference's own
 * expression. */
inline Stabilisers make_stabilisers(unsigned bpc)
{
    const int samplemax = (1 << bpc) - 1;
    const double sm = (double)samplemax;
    return {.k1 = signed_from_bits(std::bit_cast<uint64_t>(sm * sm * kSsimK1)),
            .k2 = signed_from_bits(std::bit_cast<uint64_t>(sm * sm * kSsimK2))};
}

/* The weight of a window that lies inside the frame: the kernel's taps sum to
 * 256 in each direction. */
inline constexpr uint64_t kFullWeight = 65536u;
inline constexpr int32_t kFullWeightLog2 = 16;

/* fl64(a * w_d) for a window weight. Every window that lies inside the frame
 * has the weight 2^16, and a product with a power of two is exact: only a
 * window the frame's edge truncates takes the multiplication. */
VMAF_SYCL_ALWAYS_INLINE SoftSigned times_weight(SoftSigned a, uint64_t w)
{
    uint64_t mant = a.mant;
    int32_t exp = a.exp + kFullWeightLog2;
    if (w != kFullWeight) {
        const SoftSigned product = signed_mul(a, signed_from_u64(w));
        mant = product.mant;
        exp = product.exp;
    }
    return vmaf_sycl_soft::signed_make(mant, exp, a.negative != 0u);
}

/* The six products of two integers in the reference's expression, each exact
 * in 64 bits: mux * mux, mux * muy, muy * muy and x2, xy, y2 times the
 * weight. */
struct Products {
    uint64_t mx2;
    uint64_t mxy;
    uint64_t my2;
    uint64_t x2w;
    uint64_t xyw;
    uint64_t y2w;
};

VMAF_SYCL_ALWAYS_INLINE Products products(const Moments &m)
{
    return {.mx2 = m.mux * m.mux,
            .mxy = m.mux * m.muy,
            .my2 = m.muy * m.muy,
            .x2w = m.x2 * m.w,
            .xyw = m.xy * m.w,
            .y2w = m.y2 * m.w};
}

/* The four values of the reference's expression that the products alone
 * make, before a stabiliser is added:
 *
 *     2 * mxy
 *     2 * (m.xy * w_d - mxy)
 *     mx2 + my2
 *     m.x2 * w_d - mx2 + m.y2 * w_d - my2
 */
struct ProductSums {
    SoftSigned twice_mxy;
    SoftSigned twice_covariance;
    SoftSigned mean_squares;
    SoftSigned variances;
};

/* As the reference forms them at any bit depth: each product is rounded to
 * fp64 and each sum and difference rounds again, left to right. */
VMAF_SYCL_ALWAYS_INLINE ProductSums product_sums_rounded(const Products &p)
{
    const SoftSigned mx2 = signed_from_u64(p.mx2);
    const SoftSigned mxy = signed_from_u64(p.mxy);
    const SoftSigned my2 = signed_from_u64(p.my2);
    const SoftSigned reference_variance = signed_sub(signed_from_u64(p.x2w), mx2);
    const SoftSigned both = signed_add(reference_variance, signed_from_u64(p.y2w));
    return {.twice_mxy = signed_twice(mxy),
            .twice_covariance = signed_twice(signed_sub(signed_from_u64(p.xyw), mxy)),
            .mean_squares = signed_add(mx2, my2),
            .variances = signed_sub(both, my2)};
}

/* The products below this bound make sums the reference does not round. */
inline constexpr uint64_t kExactProductBound = uint64_t{1} << 52;

/* Whether every product is below 2^52. Then each is an fp64 value as it
 * stands, and so is every sum and difference of the four expressions: an
 * integer below 2^53. That is every window at 8 and 10 bits (mux, muy < 2^26;
 * x2, xy, y2 < 2^36). */
VMAF_SYCL_ALWAYS_INLINE bool products_are_exact(const Products &p)
{
    return (p.mx2 | p.mxy | p.my2 | p.x2w | p.xyw | p.y2w) < kExactProductBound;
}

/* The same four values when products_are_exact(): integer arithmetic, and one
 * conversion each that does not round. The variances are not negative: a
 * weighted mean of squares is at least the square of the weighted mean. */
VMAF_SYCL_ALWAYS_INLINE ProductSums product_sums_exact(const Products &p)
{
    const bool anticorrelated = p.xyw < p.mxy;
    const uint64_t covariance = anticorrelated ? p.mxy - p.xyw : p.xyw - p.mxy;
    const SoftSigned twice_covariance = vmaf_sycl_soft::signed_from_exact(2u * covariance);
    return {.twice_mxy = vmaf_sycl_soft::signed_from_exact(2u * p.mxy),
            .twice_covariance = vmaf_sycl_soft::signed_make(twice_covariance.mant,
                                                            twice_covariance.exp, anticorrelated),
            .mean_squares = vmaf_sycl_soft::signed_from_exact(p.mx2 + p.my2),
            .variances = vmaf_sycl_soft::signed_from_exact(p.x2w - p.mx2 + p.y2w - p.my2)};
}

/* The fp64 bit pattern of the term the reference adds to `*ssim`. */
VMAF_SYCL_ALWAYS_INLINE uint64_t term_bits(const Moments &m, const Stabilisers &k)
{
    const SoftSigned c1 = times_weight(times_weight(k.k1, m.w), m.w);
    const SoftSigned c2 = times_weight(times_weight(k.k2, m.w), m.w);
    const Products p = products(m);
    ProductSums sums = product_sums_exact(p);
    if (!products_are_exact(p)) {
        sums = product_sums_rounded(p);
    }
    /* m.w * (2 * mxy + c1) * (c2 + 2 * (m.xy * w_d - mxy)) */
    const SoftSigned means = times_weight(signed_add(sums.twice_mxy, c1), m.w);
    const SoftSigned numerator = signed_mul(means, signed_add(c2, sums.twice_covariance));
    /* (mx2 + my2 + c1) * (m.x2 * w_d - mx2 + m.y2 * w_d - my2 + c2) */
    const SoftSigned denominator =
        signed_mul(signed_add(sums.mean_squares, c1), signed_add(sums.variances, c2));
    return signed_bits(signed_div(numerator, denominator));
}

} // namespace vmaf_sycl_issim

#endif /* VMAF_FEATURE_SYCL_SYCL_INTEGER_SSIM_MATH_H_ */
