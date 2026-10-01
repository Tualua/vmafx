/**
 *  Copyright 2016-2020 Netflix, Inc.
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 *  The two integers integer_vif.c derives from a pixel's gain, for SYCL
 *  kernels, without the fp64 type (ADR-0220). The reference
 *  (integer_vif.c::vif_accumulate_pixel(), and the same lines in
 *  x86/vif_avx2.c and x86/vif_avx512.c) computes
 *
 *      const double eps = 65536 * 1.0e-10;
 *      double g = sigma12 / (sigma1_sq + eps);
 *      int32_t sv_sq = sigma2_sq - g * sigma12;
 *      sv_sq = (uint32_t)(MAX(sv_sq, 0));
 *      g = MIN(g, vif_enhn_gain_limit);
 *      ... (int64_t)((g * g * sigma1_sq)) ...
 *
 *  Both results are truncations of fp64 values. In exact arithmetic the two
 *  values are quotients of integers by `sigma1_sq`, moved by a term in `eps`:
 *
 *      sigma2_sq - sigma12^2 / (sigma1_sq + eps)
 *      sigma12^2 * sigma1_sq / (sigma1_sq + eps)^2
 *
 *  so one integer division gives both integer parts, unless the exact value
 *  lies within the fp64 chain's own rounding error of an integer. Those
 *  samples (one pixel in about 300 000 on real content) replay the
 *  reference's fp64 operations in 64-bit integers (sycl_soft_double.h), so
 *  the result is the reference's on every sample by construction
 *  (ADR-1432).
 *
 *  Preconditions, which are integer_vif.c's for these lines: sigma1_sq is at
 *  least 2 * 65536, sigma2_sq and sigma12 are positive, all three are below
 *  2^31, and the gain limit is at least 1. Kernel code may use everything
 *  here except make_gain_limit(), which is host code and uses fp64.
 */

#ifndef VMAF_FEATURE_SYCL_SYCL_INTEGER_VIF_MATH_H_
#define VMAF_FEATURE_SYCL_SYCL_INTEGER_VIF_MATH_H_

#include <sycl/sycl.hpp>

#include <cmath>
#include <cstdint>

#include "sycl_soft_double.h"

namespace vmaf_sycl_ivif
{

using vmaf_sycl_soft::round_kept;
using vmaf_sycl_soft::Shifted;
using vmaf_sycl_soft::soft_div;
using vmaf_sycl_soft::soft_from_u32;
using vmaf_sycl_soft::soft_less;
using vmaf_sycl_soft::soft_mul;
using vmaf_sycl_soft::soft_sub_trunc;
using vmaf_sycl_soft::soft_trunc;
using vmaf_sycl_soft::SoftDouble;

/* 65536 * 1.0e-10, the reference's `eps`, as kEpsMant * 2^kEpsExp. */
inline constexpr uint64_t kEpsMant = 0x1b7cdfd9d7bdbbULL;
inline constexpr int32_t kEpsExp = -70;

/* fl64(sigma1_sq + eps): the sum stays in sigma1_sq's binade, so it is
 * sigma1_sq with eps rounded to that binade's last place. */
struct Divisor {
    SoftDouble value;
    uint32_t eps_units; /* the rounded eps, in units of the last place */
    int32_t binade;     /* floor(log2(sigma1_sq)) */
};

inline Divisor divisor_of(uint32_t sigma1_sq)
{
    const int32_t binade = 31 - (int32_t)sycl::clz(sigma1_sq);
    /* The last place of the sum is 2^(binade - 52). */
    const uint32_t shift = (uint32_t)(binade - 52 - kEpsExp);
    const Shifted eps = {.kept = kEpsMant >> shift,
                         .half = ((kEpsMant >> (shift - 1u)) & 1u) != 0u,
                         .sticky = (kEpsMant & ((uint64_t{1} << (shift - 1u)) - 1u)) != 0u};
    const uint64_t units = round_kept(eps);
    const uint64_t mant = ((uint64_t)sigma1_sq << (uint32_t)(52 - binade)) + units;
    return {.value = {.mant = mant, .exp = binade - 52},
            .eps_units = (uint32_t)units,
            .binade = binade};
}

/* ------------------------------------------------------------------ */
/* The gain limit as the kernels take it                               */
/* ------------------------------------------------------------------ */

struct GainLimit {
    SoftDouble value;   /* vif_enhn_gain_limit */
    SoftDouble squared; /* fl64(limit * limit) */
    /* The limit when it is an integer below 2^11, else 0. `g < limit` is then
     * an integer comparison, and limit * limit * sigma1_sq an integer below
     * 2^53, which fp64 holds exactly. */
    uint32_t integer;
    float hi; /* the limit as a pair, for the other limits */
    float lo;
};

struct GainTerms {
    uint32_t sv_sq;   /* (uint32_t)MAX((int32_t)(sigma2_sq - g * sigma12), 0) */
    int64_t gg_sigma; /* (int64_t)(g * g * sigma1_sq), g after the limit */
};

/* ------------------------------------------------------------------ */
/* The reference's operations, replayed                                */
/* ------------------------------------------------------------------ */

inline GainTerms gain_terms_replayed(uint32_t sigma1_sq, uint32_t sigma2_sq, uint32_t sigma12,
                                     const GainLimit &limit)
{
    const SoftDouble covariance = soft_from_u32(sigma12);
    const SoftDouble g = soft_div(covariance, divisor_of(sigma1_sq).value);
    const SoftDouble t = soft_mul(g, covariance);
    const bool positive = soft_less(t, soft_from_u32(sigma2_sq));
    const uint32_t sv_sq = positive ? soft_sub_trunc(sigma2_sq, t) : 0u;
    const bool unlimited = soft_less(g, limit.value);
    const uint64_t g_mant = unlimited ? g.mant : limit.value.mant;
    const int32_t g_exp = unlimited ? g.exp : limit.value.exp;
    const SoftDouble g_limited = {.mant = g_mant, .exp = g_exp};
    const SoftDouble squared = soft_mul(g_limited, g_limited);
    const SoftDouble scaled = soft_mul(squared, soft_from_u32(sigma1_sq));
    return {.sv_sq = sv_sq, .gg_sigma = soft_trunc(scaled)};
}

/* ------------------------------------------------------------------ */
/* The integer evaluation                                              */
/* ------------------------------------------------------------------ */

struct Quotient {
    uint64_t quot;
    uint32_t rem;
};

/* floor(p / d) and the remainder for p below 2^62 and d in [2^17, 2^31).
 * Two fp32 estimates and an integer correction: a GPU has no 64-bit divider. */
inline Quotient divide(uint64_t p, uint32_t d)
{
    const float df = (float)d;
    const auto divisor = (int64_t)d;
    const uint64_t first = (uint64_t)((float)p / df);
    int64_t rem = (int64_t)p - (int64_t)(first * d);
    const int64_t second = (int64_t)((float)rem / df);
    int64_t quot = (int64_t)first + second;
    rem -= second * divisor;
#pragma unroll
    for (int fix = 0; fix < 4; fix++) {
        const bool low = rem < 0;
        const bool high = rem >= divisor;
        quot += high ? 1 : (low ? -1 : 0);
        rem += low ? divisor : (high ? -divisor : int64_t{0});
    }
    return {.quot = (uint64_t)quot, .rem = (uint32_t)rem};
}

struct GainResult {
    GainTerms terms;
    bool replay; /* the integer evaluation does not decide: take the replay */
};

/* `g < limit` and whether that decision is certain. */
struct LimitTest {
    bool unlimited;
    bool certain;
};

inline LimitTest limit_test(uint32_t sigma1_sq, uint32_t sigma12, float eps_r,
                            const GainLimit &limit)
{
    if (limit.integer != 0u) {
        /* sigma12 / (sigma1_sq + eps) < L  <=>  sigma12 <= L * sigma1_sq: the
         * two sides of the first differ by at least eps / sigma1_sq, far more
         * than an fp64 rounding. */
        return {.unlimited = (uint64_t)sigma12 <= (uint64_t)limit.integer * sigma1_sq,
                .certain = true};
    }
    /* Any other limit: compare in fp32 and trust the comparison only when it
     * is clear. limit * (sigma1_sq + eps) against sigma12. */
    const float bound = limit.hi * ((float)sigma1_sq + eps_r) + limit.lo * (float)sigma1_sq;
    const float value = (float)sigma12;
    const float margin = value * 0x1p-18f;
    return {.unlimited = value<bound, .certain = sycl::fabs(value - bound)> margin};
}

/* Fractional bits of the fixed-point remainders below. */
inline constexpr uint32_t kSub = 8u;

inline GainResult gain_terms_integer(uint32_t sigma1_sq, uint32_t sigma2_sq, uint32_t sigma12,
                                     const GainLimit &limit)
{
    const Divisor divisor = divisor_of(sigma1_sq);
    const uint64_t product = (uint64_t)sigma12 * sigma12;
    const Quotient q = divide(product, sigma1_sq);
    const int64_t divisor_fp = (int64_t)sigma1_sq << kSub;
    /* eps, as rounded into the divisor, and c = product * eps / divisor: what
     * eps moves the two quotients by, in units of 2^-kSub / sigma1_sq. The
     * fp32 chain is good to 2^-21 of c; the conversion drops less than one
     * unit. */
    const float eps_r = (float)divisor.eps_units * sycl::ldexp(1.0f, divisor.binade - 52);
    const float shift_f = (float)product / (float)sigma1_sq * eps_r * (float)(1u << kSub);
    const int64_t shift = (int64_t)shift_f;
    const int64_t shift_error = (shift >> 21) + 2;

    /* sigma2_sq - product / (sigma1_sq + eps) = qa + (ra + c) / sigma1_sq.
     * The fp64 chain is within 2^-20 of it (three roundings of values below
     * 2^31). Only a positive integer is a boundary: around 0 the truncation
     * gives 0 from both sides, and a negative value is clamped. */
    const int64_t qa = (int64_t)sigma2_sq - (int64_t)q.quot - (q.rem != 0u ? 1 : 0);
    const int64_t ra = q.rem != 0u ? (int64_t)sigma1_sq - (int64_t)q.rem : int64_t{0};
    const int64_t sv_zone = ((int64_t)sigma1_sq >> (20u - kSub)) + 1 + shift_error;
    const int64_t sv_frac = (ra << kSub) + shift;
    const bool carry = sv_frac >= divisor_fp;
    const bool sv_low = qa >= 1 && sv_frac < sv_zone;
    const int64_t sv_gap = sv_frac - divisor_fp;
    const bool sv_high = qa >= 0 && sv_gap < sv_zone && sv_gap > -sv_zone;
    const uint32_t sv_sq = qa >= 0 ? (uint32_t)(qa + (carry ? 1 : 0)) : 0u;

    const LimitTest test = limit_test(sigma1_sq, sigma12, eps_r, limit);
    /* g * g * sigma1_sq = quot + (rem - 2c) / sigma1_sq below the limit; the
     * fp64 chain is within 2^-51 of it, relatively (four roundings). */
    const int64_t gg_zone = (int64_t)(product >> (51u - kSub)) + 1 + 2 * shift_error;
    const int64_t gg_frac = ((int64_t)q.rem << kSub) - 2 * shift;
    const bool borrow = gg_frac < 0;
    const bool gg_near = (gg_frac < gg_zone && gg_frac > -gg_zone) ||
                         gg_frac > divisor_fp - gg_zone || gg_frac < gg_zone - divisor_fp;
    const int64_t unlimited_gg = (int64_t)q.quot - (borrow ? 1 : 0);
    /* At an integer limit the product is exact. Any other limit rounds, and
     * that product is left to the replay. */
    const int64_t limited_gg = (int64_t)((uint64_t)limit.integer * limit.integer) * sigma1_sq;
    const bool limited_rounds = !test.unlimited && limit.integer == 0u;

    const bool replay =
        sv_low || sv_high || !test.certain || (test.unlimited && gg_near) || limited_rounds;
    return {.terms = {.sv_sq = sv_sq, .gg_sigma = test.unlimited ? unlimited_gg : limited_gg},
            .replay = replay};
}

/* The two integers of integer_vif.c for one pixel of the log branch. */
inline GainTerms gain_terms(uint32_t sigma1_sq, uint32_t sigma2_sq, uint32_t sigma12,
                            const GainLimit &limit)
{
    const GainResult fast = gain_terms_integer(sigma1_sq, sigma2_sq, sigma12, limit);
    if (!fast.replay) {
        return fast.terms;
    }
    return gain_terms_replayed(sigma1_sq, sigma2_sq, sigma12, limit);
}

/* Host code: the limit in the forms above. */
inline GainLimit make_gain_limit(double limit)
{
    GainLimit out = {};
    int exponent = 0;
    const double fraction = std::frexp(limit, &exponent);
    out.value = {.mant = (uint64_t)std::ldexp(fraction, 53), .exp = exponent - 53};
    const double squared = limit * limit;
    const double squared_fraction = std::frexp(squared, &exponent);
    out.squared = {.mant = (uint64_t)std::ldexp(squared_fraction, 53), .exp = exponent - 53};
    const bool is_integer = limit == std::floor(limit) && limit < 2048.0;
    out.integer = is_integer ? (uint32_t)limit : 0u;
    out.hi = (float)limit;
    out.lo = (float)(limit - (double)out.hi);
    return out;
}

} // namespace vmaf_sycl_ivif

#endif /* VMAF_FEATURE_SYCL_SYCL_INTEGER_VIF_MATH_H_ */
