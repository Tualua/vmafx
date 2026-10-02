/**
 *  Copyright 2016-2026 Netflix, Inc.
 *  Copyright (c) 2011, Tom Distler (http://tdistler.com)
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: BSD-2-Clause-Patent AND BSD-3-Clause
 *
 *  The CPU's SSIM window arithmetic for SYCL kernels, without fp64
 *  (ADR-0220): iqa/convolve.c's two Gaussian passes and
 *  iqa/ssim_tools.c's per-pixel luminance, contrast and structure terms,
 *  operation for operation and type for type. The CPU's fp64 values of the
 *  convolution are carried as exact fp32 pairs (sycl_exact_fp.h). Its fp64
 *  terms come in two forms: the doubles themselves, computed in 64-bit
 *  integers and added on the host in the CPU's raster order
 *  (ssim_double_terms(), ssim_frame_sums(); float_ssim_sycl, ADR-1463), and
 *  pairs added as int64 fixed point (ssim_terms(), term_fixed();
 *  float_ms_ssim_sycl, ADR-1414).
 *
 *  Shared by float_ssim_sycl (integer_ssim_sycl.cpp) and float_ms_ssim_sycl
 *  (integer_ms_ssim_sycl.cpp): one implementation of the arithmetic, so a
 *  change to the CPU reference is mirrored once. Every function assumes its translation unit is compiled
 *  with contraction off, which every SYCL feature TU is (ADR-1367). This
 *  header holds no kernel and may be included by host code of those TUs.
 */

#ifndef VMAF_FEATURE_SYCL_SYCL_SSIM_TERMS_H_
#define VMAF_FEATURE_SYCL_SYCL_SSIM_TERMS_H_

#include <sycl/sycl.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>

#include "sycl_compat.h"
#include "sycl_exact_fp.h"
#include "sycl_soft_signed.h"

namespace vmaf_sycl_ssim
{

using vmaf_sycl_exact::div_rn;
using vmaf_sycl_exact::Ff;
using vmaf_sycl_exact::ff_add;
using vmaf_sycl_exact::ff_div;
using vmaf_sycl_exact::ff_mul;
using vmaf_sycl_exact::sqrt_rn;
using vmaf_sycl_exact::two_prod;
using vmaf_sycl_exact::two_sum;

/* Frame reduction (Research-2133): every per-pixel term goes to int64 in
 * units of 2^-52 before the work-group sum. |term| <= 2 and a work-group has
 * 128 items, so a group sum stays below 2^60, and integer addition makes the
 * sum exact and independent of the reduction order. */
inline constexpr float SSIM_TERM_FIXED_ONE = 0x1p52f;
inline constexpr double SSIM_TERM_FIXED_INV = 0x1p-52;

struct SsimMoments {
    float reference_mean;
    float comparison_mean;
    float reference_square;
    float comparison_square;
    float cross_product;
};

/* The five sums of one convolution pass, as pairs (Research-2133). */
struct MomentPairs {
    Ff reference_mean;
    Ff comparison_mean;
    Ff reference_square;
    Ff comparison_square;
    Ff cross_product;
};

/* One pixel's luminance, contrast and structure terms in the CPU's types:
 * L and C are doubles on the CPU (pairs here), S is an fp32 quotient. */
struct SsimTerms {
    Ff luminance;
    Ff contrast;
    float structure;
};

/* One tap of iqa/convolve.c: the fp32 product `img * kernel`, added to the
 * pass's sum. The CPU adds these products in double and rounds the sum to
 * fp32 once; a pair sum carries it to about 2^-46, so the rounded pair is the
 * CPU's fp32 value except within that distance of a rounding midpoint. */
inline Ff add_tap(Ff sum, float sample, float weight)
{
    const float product = sample * weight;
    return ff_add(sum, Ff{.hi = product, .lo = 0.0f});
}

/* Horizontal pass: the CPU convolves ref, cmp and the fp32 products
 * ref * ref, cmp * cmp and ref * cmp (ssim_precompute). */
inline void add_horizontal_tap(MomentPairs &sums, float ref, float cmp, float weight)
{
    const float ref_sq = ref * ref;
    const float cmp_sq = cmp * cmp;
    const float ref_cmp = ref * cmp;
    sums.reference_mean = add_tap(sums.reference_mean, ref, weight);
    sums.comparison_mean = add_tap(sums.comparison_mean, cmp, weight);
    sums.reference_square = add_tap(sums.reference_square, ref_sq, weight);
    sums.comparison_square = add_tap(sums.comparison_square, cmp_sq, weight);
    sums.cross_product = add_tap(sums.cross_product, ref_cmp, weight);
}

/* Vertical pass over the five horizontal results. */
inline void add_vertical_tap(MomentPairs &sums, const SsimMoments &row, float weight)
{
    sums.reference_mean = add_tap(sums.reference_mean, row.reference_mean, weight);
    sums.comparison_mean = add_tap(sums.comparison_mean, row.comparison_mean, weight);
    sums.reference_square = add_tap(sums.reference_square, row.reference_square, weight);
    sums.comparison_square = add_tap(sums.comparison_square, row.comparison_square, weight);
    sums.cross_product = add_tap(sums.cross_product, row.cross_product, weight);
}

/* A pass result, `(float)(sum * scale)` with scale 1 on the CPU: ff_add keeps
 * a pair normalised, so hi is hi + lo rounded to fp32. */
inline SsimMoments round_moments(const MomentPairs &sums)
{
    return {.reference_mean = sums.reference_mean.hi,
            .comparison_mean = sums.comparison_mean.hi,
            .reference_square = sums.reference_square.hi,
            .comparison_square = sums.comparison_square.hi,
            .cross_product = sums.cross_product.hi};
}

/* The fp32 values iqa/ssim_tools.c forms for one pixel before its fp64
 * terms: the arguments of ssim_accumulate_lane() (ssim_accumulate_lane.h). */
struct SsimFloatParts {
    float reference_mean;
    float comparison_mean;
    float srsc;      /* sqrtf(ref_sigma_sqd * cmp_sigma_sqd) */
    float l_den;     /* ref_mu^2 + cmp_mu^2 + C1 */
    float c_den;     /* ref_sigma_sqd + cmp_sigma_sqd + C2 */
    float structure; /* sv_f, the fp32 quotient S */
};

/* iqa/ssim_tools.c for one pixel (ssim_variance_scalar, then the fp32 part
 * of ssim_accumulate_default_scalar, which ssim_accumulate_lane.h shares with
 * the SIMD paths): fp32 variances clamped at zero, the covariance, one fp32
 * square root of their product, the fp32 denominators and S. Every fp32
 * operation is the CPU's, in its order: contraction is off for every SYCL
 * feature TU (sycl_strict_fp_args, ADR-1367) and the division and square root
 * are correctly rounded. There is no identical-window shortcut: on a flat
 * identical window the fp32 l_den rounds below 2 * mu^2 + C1, and the CPU
 * keeps that. */
inline SsimFloatParts ssim_float_parts(const SsimMoments &m, float c1, float c2)
{
    const float ref_mean_sq = m.reference_mean * m.reference_mean;
    const float cmp_mean_sq = m.comparison_mean * m.comparison_mean;
    const float mean_product = m.reference_mean * m.comparison_mean;
    const float ref_var_raw = m.reference_square - ref_mean_sq;
    const float cmp_var_raw = m.comparison_square - cmp_mean_sq;
    const float ref_var = ref_var_raw < 0.0f ? 0.0f : ref_var_raw;
    const float cmp_var = cmp_var_raw < 0.0f ? 0.0f : cmp_var_raw;
    const float covariance = m.cross_product - mean_product;
    const float var_product = ref_var * cmp_var;
    const float srsc = sqrt_rn(var_product);
    const float l_den_sum = ref_mean_sq + cmp_mean_sq;
    const float l_den = l_den_sum + c1;
    const float c_den_sum = ref_var + cmp_var;
    const float c_den = c_den_sum + c2;
    const float c3 = c2 / 2.0f;
    const float flat_covariance = (covariance < 0.0f && srsc <= 0.0f) ? 0.0f : covariance;
    const float s_num = flat_covariance + c3;
    const float s_den = srsc + c3;
    return {.reference_mean = m.reference_mean,
            .comparison_mean = m.comparison_mean,
            .srsc = srsc,
            .l_den = l_den,
            .c_den = c_den,
            .structure = div_rn(s_num, s_den)};
}

/* L = (2.0 * mu_ref * mu_cmp + C1) / l_den and C = (2.0 * srsc + C2) / c_den,
 * which the CPU forms in double, as pairs (about 2^-44 from the double), with
 * the fp32 S. float_ms_ssim_sycl sums these (ADR-1414); float_ssim_sycl takes
 * the doubles themselves (ssim_double_terms() below). */
inline SsimTerms ssim_terms(const SsimMoments &m, float c1, float c2)
{
    const SsimFloatParts p = ssim_float_parts(m, c1, c2);
    const Ff product = two_prod(p.reference_mean, p.comparison_mean);
    const Ff doubled = {.hi = 2.0f * product.hi, .lo = 2.0f * product.lo};
    const Ff l_num = ff_add(doubled, Ff{.hi = c1, .lo = 0.0f});
    const Ff c_num = two_sum(2.0f * p.srsc, c2);
    return {.luminance = ff_div(l_num, Ff{.hi = p.l_den, .lo = 0.0f}),
            .contrast = ff_div(c_num, Ff{.hi = p.c_den, .lo = 0.0f}),
            .structure = p.structure};
}

/* One pixel's terms as the CPU's own values: `lv` and `cv` of
 * ssim_accumulate_lane() are doubles, held here as a significand and an
 * exponent in integers (sycl_soft_signed.h; a kernel has no fp64 type,
 * ADR-0220), and `sv` is the fp32 quotient converted. */
struct SsimDoubleTerms {
    vmaf_sycl_soft::SoftSigned luminance;
    vmaf_sycl_soft::SoftSigned contrast;
    float structure;
};

/* ssim_accumulate_lane()'s two fp64 expressions, operation for operation:
 *
 *     const double lv = (2.0 * rm * cm + C1) / l_den;
 *     const double cv = (2.0 * srsc + C2) / c_den;
 *
 * `2.0 * rm * cm` is exact in fp64 (two 24-bit significands), so lv is one
 * rounded sum and one rounded quotient, and so is cv. Both numerators and
 * denominators are positive: the means and srsc are not negative and C1 and
 * C2 are positive. */
VMAF_SYCL_ALWAYS_INLINE SsimDoubleTerms ssim_double_terms(const SsimFloatParts &p, float c1,
                                                          float c2)
{
    using vmaf_sycl_soft::signed_add;
    using vmaf_sycl_soft::signed_div;
    using vmaf_sycl_soft::signed_from_float;
    using vmaf_sycl_soft::signed_mul;
    using vmaf_sycl_soft::signed_twice;
    using vmaf_sycl_soft::SoftSigned;
    const SoftSigned mean_product =
        signed_mul(signed_from_float(p.reference_mean), signed_from_float(p.comparison_mean));
    const SoftSigned l_num = signed_add(signed_twice(mean_product), signed_from_float(c1));
    const SoftSigned c_num =
        signed_add(signed_twice(signed_from_float(p.srsc)), signed_from_float(c2));
    return {.luminance = signed_div(l_num, signed_from_float(p.l_den)),
            .contrast = signed_div(c_num, signed_from_float(p.c_den)),
            .structure = p.structure};
}

/* The fp64 bit pattern of the pixel's SSIM term, ssim_accumulate_lane()'s
 * `lv * cv * sv`: two rounded fp64 products, left to right. */
VMAF_SYCL_ALWAYS_INLINE std::uint64_t ssim_product_bits(const SsimDoubleTerms &t)
{
    using vmaf_sycl_soft::signed_bits;
    using vmaf_sycl_soft::signed_from_float;
    using vmaf_sycl_soft::signed_mul;
    return signed_bits(
        signed_mul(signed_mul(t.luminance, t.contrast), signed_from_float(t.structure)));
}

/* iqa_ssim()'s four frame sums (host side). */
struct SsimFrameSums {
    double ssim;
    double luminance;
    double contrast;
    double structure;
};

/* ssim_accumulate_lane() for one window, with its lv, cv and sv (host side):
 * each sum is one double that takes the windows in the caller's order. */
inline void accumulate_window(SsimFrameSums &sums, double lv, double cv, double sv)
{
    sums.ssim += lv * cv * sv;
    sums.luminance += lv;
    sums.contrast += cv;
    sums.structure += sv;
}

/* The four sums over a plane's windows in raster order (host side), from the
 * per-window lv and cv bit patterns and the fp32 sv a kernel stored: the
 * CPU's sums, since the terms and the order are the CPU's. */
inline SsimFrameSums ssim_frame_sums(const std::uint64_t *luminance, const std::uint64_t *contrast,
                                     const float *structure, std::size_t count)
{
    SsimFrameSums sums = {};
    for (std::size_t i = 0U; i < count; i++) {
        accumulate_window(sums, std::bit_cast<double>(luminance[i]),
                          std::bit_cast<double>(contrast[i]), (double)structure[i]);
    }
    return sums;
}

/* The pixel's SSIM term, `lv * cv * sv` in double on the CPU, as a pair. */
inline Ff ssim_term(const SsimTerms &t)
{
    return ff_mul(ff_mul(t.luminance, t.contrast), Ff{.hi = t.structure, .lo = 0.0f});
}

/* A pair in int64 units of 2^-52, rounded to nearest: hi * 2^52 is exact and
 * integral for |hi| >= 2^-28, lo adds its rounded share. */
inline std::int64_t term_fixed(Ff value)
{
    const float hi = sycl::rint(value.hi * SSIM_TERM_FIXED_ONE);
    const float lo = sycl::rint(value.lo * SSIM_TERM_FIXED_ONE);
    return static_cast<std::int64_t>(hi) + static_cast<std::int64_t>(lo);
}

/* Exact sum of fixed-point terms of any count (host side): each is split
 * into multiples of 2^32 and a remainder, so neither half overflows, and the
 * halves join in one double rounding. */
class FixedSum
{
  public:
    void add(std::int64_t value)
    {
        const std::int64_t value_high = value / 0x100000000LL;
        high += value_high;
        low += value - value_high * 0x100000000LL;
    }

    [[nodiscard]] double value() const
    {
        return ((double)high * 0x1p32 + (double)low) * SSIM_TERM_FIXED_INV;
    }

  private:
    std::int64_t high = 0;
    std::int64_t low = 0;
};

/* ssim_init_args' C1 and C2 for L = 255, K1 = 0.01, K2 = 0.03, in fp32. */
inline void float_ssim_constants(float *c1, float *c2)
{
    const float range = 255.0f;
    const float k1 = 0.01f;
    const float k2 = 0.03f;
    *c1 = (k1 * range) * (k1 * range);
    *c2 = (k2 * range) * (k2 * range);
}

} // namespace vmaf_sycl_ssim

#endif /* VMAF_FEATURE_SYCL_SYCL_SSIM_TERMS_H_ */
