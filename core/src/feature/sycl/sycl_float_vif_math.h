/**
 *  Copyright 2016-2020 Netflix, Inc.
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 *  The CPU's float VIF pixel statistic for SYCL kernels, without the fp64
 *  type (ADR-0220): vif_tools.c::vif_pixel_statistic_s() and
 *  log2f_approx(), operation for operation.
 *
 *  Two expressions of the reference are fp64, because `vif_sigma_nsq` is a
 *  `double` parameter:
 *
 *      1.0f + (g * g * sigma1_sq) / (sv_sq + vif_sigma_nsq)
 *      1.0f + (sigma1_sq) / (vif_sigma_nsq)
 *
 *  and each is rounded to fp32 once, when it is passed to log2f(). Here both
 *  are evaluated as exact fp32 pairs (sycl_exact_fp.h), good to about 2^-44,
 *  and rounded to fp32. That is the reference's value unless the pair lies
 *  within 2^-12 of an fp32 unit of a rounding boundary; for those samples
 *  (about one in 2000) the reference's own sequence of fp64 operations is
 *  replayed in 64-bit integers (sycl_soft_double.h), so the result is the
 *  reference's on every sample by construction, not only in practice.
 *
 *  Every function assumes its translation unit is compiled with contraction
 *  off, which every SYCL feature TU is (ADR-1367). Kernel code may use
 *  everything here except make_noise_variance() and make_statistic_params(),
 *  which are host code and use fp64.
 */

#ifndef VMAF_FEATURE_SYCL_SYCL_FLOAT_VIF_MATH_H_
#define VMAF_FEATURE_SYCL_SYCL_FLOAT_VIF_MATH_H_

#include <sycl/sycl.hpp>

#include <cmath>
#include <cstdint>
#include <limits>

#include "sycl_exact_fp.h"
#include "sycl_soft_double.h"

namespace vmaf_sycl_fvif
{

using vmaf_sycl_exact::Ff;
using vmaf_sycl_exact::ff_add;
using vmaf_sycl_exact::ff_div;
using vmaf_sycl_soft::kDoubleTop;
using vmaf_sycl_soft::soft_add;
using vmaf_sycl_soft::soft_div;
using vmaf_sycl_soft::soft_from_float;
using vmaf_sycl_soft::soft_to_float;
using vmaf_sycl_soft::SoftDouble;

/* ------------------------------------------------------------------ */
/* log2f_approx()                                                      */
/* ------------------------------------------------------------------ */

/* vif_tools.c::log2f_approx(): the exponent plus horner_s() over
 * log2_poly_s in `mantissa - 1`. The coefficients are the reference's
 * decimal literals, rounded to fp32 through fp64 as the C initialiser of a
 * `float` array rounds them. The polynomial starts from 0, so its first step
 * is the first coefficient; the ninth coefficient is 0 and its add is kept. */
inline float log2_approx(float x)
{
    if (x == 0.0f) {
        return -std::numeric_limits<float>::infinity();
    }
    if (x < 0.0f) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    const auto bits = sycl::bit_cast<uint32_t>(x);
    const uint32_t exponent = (bits & 0x7F800000u) >> 23;
    const float remain = sycl::bit_cast<float>((bits & 0x007FFFFFu) | 0x3F800000u);
    const float log_base = (float)((int32_t)exponent - 127);
    const float t = remain - 1.0f;
    float var = (float)-0.012671635276421;
    var = var * t + (float)0.064841182402670;
    var = var * t + (float)-0.157048836463065;
    var = var * t + (float)0.257167726303123;
    var = var * t + (float)-0.353800560300520;
    var = var * t + (float)0.480131410397451;
    var = var * t + (float)-0.721314327952201;
    /* The reference's coefficient, which is close to log2(e) and is not it.
     * Upstream-parity literal of vif_tools.c::log2_poly_s (ADR-1422). */
    // NOLINTNEXTLINE(modernize-use-std-numbers)
    var = var * t + (float)1.442694803896991;
    var = var * t + 0.0f;
    return log_base + var;
}

/* ------------------------------------------------------------------ */
/* `vif_sigma_nsq` as the kernels take it                              */
/* ------------------------------------------------------------------ */

struct NoiseVariance {
    float hi; /* the fp64 value as a pair, to about 2^-48 */
    float lo;
    float above;   /* the smallest fp32 value that is not below it */
    uint64_t mant; /* the fp64 value itself (SoftDouble); 0 when it is zero */
    int32_t exp;
};

/* ------------------------------------------------------------------ */
/* 1.0f + numerator / denominator, in the reference's fp64             */
/* ------------------------------------------------------------------ */

/* True when the pair is within 2^-12 of an fp32 unit of the point where its
 * rounding to fp32 changes: fl32(hi + lo) is then not certain to be the
 * reference's value. `sum` is normalised (|lo| is at most half a unit of
 * hi) and at least 1. */
inline bool near_rounding_boundary(Ff sum)
{
    const auto bits = sycl::bit_cast<uint32_t>(sum.hi);
    const uint32_t exponent = bits & 0x7F800000u;
    const float unit = sycl::bit_cast<float>(exponent - (uint32_t{23} << 23));
    /* Below a power of two the spacing halves. */
    const bool lower_binade = (bits & 0x007FFFFFu) == 0u && sum.lo < 0.0f;
    const float half = lower_binade ? 0.25f * unit : 0.5f * unit;
    return sycl::fabs(sum.lo) >= half - unit * 0x1p-12f;
}

/* A positive fp64 denominator in the two forms the quotient needs: as a
 * pair, and exactly. `exact.mant` is 0 for a zero denominator. */
struct Denominator {
    Ff pair;
    SoftDouble exact;
};

/* (double)vif_sigma_nsq. */
inline Denominator noise_denominator(const NoiseVariance &n)
{
    return {.pair = {.hi = n.hi, .lo = n.lo}, .exact = {.mant = n.mant, .exp = n.exp}};
}

/* fl64(sv_sq + vif_sigma_nsq), sv_sq positive and normal. */
inline Denominator noise_plus(float sv_sq, const NoiseVariance &n)
{
    const Ff pair = ff_add(Ff{.hi = sv_sq, .lo = 0.0f}, Ff{.hi = n.hi, .lo = n.lo});
    const SoftDouble addend = soft_from_float(sv_sq);
    /* A zero variance adds nothing. Scalar selects: see soft_add(). */
    const bool zero = n.mant == 0u;
    const SoftDouble sum =
        soft_add(addend, SoftDouble{.mant = zero ? kDoubleTop : n.mant, .exp = n.exp});
    return {.pair = pair,
            .exact = {.mant = zero ? addend.mant : sum.mant, .exp = zero ? addend.exp : sum.exp}};
}

/* 1.0 + numerator / denominator as an exact pair: good to about 2^-44. */
inline Ff one_plus_ratio_pair(float numerator, Ff denominator)
{
    const Ff ratio = ff_div(Ff{.hi = numerator, .lo = 0.0f}, denominator);
    if (!sycl::isfinite(ratio.hi)) {
        /* A zero denominator: the sum is the quotient's infinity or NaN. */
        return {.hi = 1.0f + ratio.hi, .lo = 0.0f};
    }
    return ff_add(Ff{.hi = 1.0f, .lo = 0.0f}, ratio);
}

/* fl32(fl64(1.0 + fl64(numerator / denominator))) by replaying the two fp64
 * operations in integers. numerator and denominator are positive and normal. */
inline float one_plus_ratio_replayed(float numerator, SoftDouble denominator)
{
    const SoftDouble ratio = soft_div(soft_from_float(numerator), denominator);
    return soft_to_float(soft_add(SoftDouble{.mant = kDoubleTop, .exp = -52}, ratio));
}

/* True when the pair does not decide the rounding. A zero, subnormal or
 * non-finite quotient is far from every boundary above 1 or not a number at
 * all, so the pair's value is the reference's there. */
inline bool needs_replay(float numerator, Ff sum, const Denominator &denominator)
{
    return sycl::isfinite(sum.hi) && near_rounding_boundary(sum) && denominator.exact.mant != 0u &&
           numerator >= (std::numeric_limits<float>::min)();
}

/* fl32(fl64(1.0 + fl64(numerator / denominator))); numerator is not
 * negative. */
inline float one_plus_ratio(float numerator, const Denominator &denominator)
{
    const Ff sum = one_plus_ratio_pair(numerator, denominator.pair);
    return needs_replay(numerator, sum, denominator) ?
               one_plus_ratio_replayed(numerator, denominator.exact) :
               sum.hi;
}

/* ------------------------------------------------------------------ */
/* vif_pixel_statistic_s()                                             */
/* ------------------------------------------------------------------ */

struct Moments {
    float mu1;
    float mu2;
    float xx;
    float yy;
    float xy;
};

struct StatisticParams {
    NoiseVariance noise;
    float gain_limit;    /* (float)vif_enhn_gain_limit */
    float sigma_max_inv; /* vif_statistic_s(): powf(vif_sigma_nsq, 2.0f) / (255.0 * 255.0) */
};

/* A pixel's variances and covariance, as vif_pixel_statistic_s() derives
 * them from the filtered moments (the two variances clamped at 0). */
struct Sigmas {
    float sigma1_sq;
    float sigma2_sq;
    float sigma12;
};

struct Term {
    float num;
    float den;
};

/* Host code: `vif_sigma_nsq` in the forms the kernels read. A pair, the fp64
 * value itself as an integer significand and exponent, and the smallest fp32
 * value not below it, which turns `sigma1_sq < vif_sigma_nsq` into an fp32
 * comparison with the same outcome. */
inline NoiseVariance make_noise_variance(double sigma_nsq)
{
    NoiseVariance noise = {};
    noise.hi = (float)sigma_nsq;
    noise.lo = (float)(sigma_nsq - (double)noise.hi);
    noise.above = noise.hi;
    if ((double)noise.above < sigma_nsq) {
        noise.above = std::nextafter(noise.above, std::numeric_limits<float>::infinity());
    }
    if (sigma_nsq > 0.0) {
        int exponent = 0;
        const double fraction = std::frexp(sigma_nsq, &exponent);
        noise.mant = (uint64_t)std::ldexp(fraction, 53);
        noise.exp = exponent - 53;
    }
    return noise;
}

/* Host code: vif_statistic_s()'s per-call constants. */
inline StatisticParams make_statistic_params(double sigma_nsq, double enhn_gain_limit)
{
    return {.noise = make_noise_variance(sigma_nsq),
            .gain_limit = (float)enhn_gain_limit,
            .sigma_max_inv = (float)(std::pow((float)sigma_nsq, 2.0f) / (255.0 * 255.0))};
}

/* The first half of vif_pixel_statistic_s(): fp32 throughout. */
inline Sigmas pixel_sigmas(const Moments &m)
{
    const float mu1_sq = m.mu1 * m.mu1;
    const float mu2_sq = m.mu2 * m.mu2;
    const float mu1_mu2 = m.mu1 * m.mu2;
    const float sigma1_sq = m.xx - mu1_sq;
    const float sigma2_sq = m.yy - mu2_sq;
    return {.sigma1_sq = sigma1_sq > 0.0f ? sigma1_sq : 0.0f,
            .sigma2_sq = sigma2_sq > 0.0f ? sigma2_sq : 0.0f,
            .sigma12 = m.xy - mu1_mu2};
}

/* The second half: the gain, the two log terms and their overrides. */
inline Term pixel_statistic(const Sigmas &sigmas, const StatisticParams &p)
{
    constexpr float eps = 1.0e-10f;
    float sigma1_sq = sigmas.sigma1_sq;
    const float sigma2_sq = sigmas.sigma2_sq;
    const float sigma12 = sigmas.sigma12;

    const float gain_den = sigma1_sq + eps;
    float g = sigma12 / gain_den;
    const float g_sigma12 = g * sigma12;
    float sv_sq = sigma2_sq - g_sigma12;
    if (sigma1_sq < eps) {
        g = 0.0f;
        sv_sq = sigma2_sq;
        sigma1_sq = 0.0f;
    }
    if (sigma2_sq < eps) {
        g = 0.0f;
        sv_sq = 0.0f;
    }
    if (g < 0.0f) {
        sv_sq = sigma2_sq;
        g = 0.0f;
    }
    sv_sq = sv_sq > eps ? sv_sq : eps;
    g = g < p.gain_limit ? g : p.gain_limit;

    /* The product is fp32; the sum, the quotient and the `1.0f +` are fp64. */
    const float gain_sq = g * g;
    const float product = gain_sq * sigma1_sq;
    Term term = {
        .num = log2_approx(one_plus_ratio(product, noise_plus(sv_sq, p.noise))),
        .den = log2_approx(one_plus_ratio(sigma1_sq, noise_denominator(p.noise))),
    };
    if (sigma12 < 0.0f) {
        term.num = 0.0f;
    }
    if (sigma1_sq < p.noise.above) {
        const float scaled = sigma2_sq * p.sigma_max_inv;
        term.num = 1.0f - scaled;
        term.den = 1.0f;
    }
    return term;
}

} // namespace vmaf_sycl_fvif

#endif /* VMAF_FEATURE_SYCL_SYCL_FLOAT_VIF_MATH_H_ */
