/**
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: EUPL-1.2
 *
 *  fp64 operations in 64-bit integers, for SYCL kernels that have to return
 *  what a CPU reference computes in `double` (ADR-0220: a kernel has no fp64
 *  type). A SoftDouble is a positive fp64 value as its 53-bit significand and
 *  an exponent; every operation here rounds to nearest, ties to even, as the
 *  fp64 operation it stands for, so a sequence of them reproduces the
 *  reference's sequence bit for bit.
 *
 *  They are slow: a division is 56 loop steps. A twin calls them for the rare
 *  sample its faster arithmetic cannot decide (sycl_float_vif_math.h,
 *  ADR-1422; sycl_integer_vif_math.h, ADR-1432), never for every pixel.
 *
 *  Kernel-safe: integers only, no arrays, and no select between structs (a
 *  struct select stays in private memory on the device, ADR-1395).
 */

#ifndef VMAF_FEATURE_SYCL_SYCL_SOFT_DOUBLE_H_
#define VMAF_FEATURE_SYCL_SYCL_SOFT_DOUBLE_H_

#include <sycl/sycl.hpp>

#include <cstdint>

namespace vmaf_sycl_soft
{

/* A positive fp64 value, mant * 2^exp with mant in [2^52, 2^53). */
struct SoftDouble {
    uint64_t mant;
    int32_t exp;
};

inline constexpr uint64_t kDoubleTop = uint64_t{1} << 52;

/* ------------------------------------------------------------------ */
/* 128-bit helpers                                                     */
/* ------------------------------------------------------------------ */

/* A 128-bit unsigned value. */
struct U128 {
    uint64_t hi;
    uint64_t lo;
};

inline U128 u128_shl(uint64_t value, uint32_t shift)
{
    /* shift < 128 */
    const uint64_t lo = shift < 64u ? value << shift : uint64_t{0};
    const uint64_t spill = (shift == 0u || shift >= 64u) ? uint64_t{0} : value >> (64u - shift);
    const uint64_t hi = shift >= 64u ? value << (shift - 64u) : spill;
    return {.hi = hi, .lo = lo};
}

inline U128 u128_sub(U128 a, uint64_t b)
{
    const uint64_t lo = a.lo - b;
    return {.hi = a.hi - (a.lo < b ? uint64_t{1} : uint64_t{0}), .lo = lo};
}

/* a * b in full. Written out in 32-bit limbs: sycl::mul_hi() on 64-bit
 * operands returned wrong values in a kernel on an Arc A380 (icpx 2026.0). */
inline U128 u128_mul(uint64_t a, uint64_t b)
{
    const uint64_t mask = 0xFFFFFFFFu;
    const uint64_t a_lo = a & mask;
    const uint64_t a_hi = a >> 32;
    const uint64_t b_lo = b & mask;
    const uint64_t b_hi = b >> 32;
    const uint64_t low = a_lo * b_lo;
    const uint64_t cross1 = a_lo * b_hi;
    const uint64_t cross2 = a_hi * b_lo;
    const uint64_t high = a_hi * b_hi;
    const uint64_t middle = (low >> 32) + (cross1 & mask) + (cross2 & mask);
    return {.hi = high + (cross1 >> 32) + (cross2 >> 32) + (middle >> 32),
            .lo = (low & mask) | (middle << 32)};
}

/* Position of the highest set bit; the value is not zero. */
inline uint32_t u128_msb(U128 a)
{
    const uint32_t hi_bits = 127u - (uint32_t)sycl::clz(a.hi | uint64_t{1});
    const uint32_t lo_bits = 63u - (uint32_t)sycl::clz(a.lo | uint64_t{1});
    return a.hi != 0u ? hi_bits : lo_bits;
}

/* a >> shift (shift < 128), and whether any dropped bit was set / the value
 * of the highest dropped bit. */
struct Shifted {
    uint64_t kept;
    bool half;   /* highest dropped bit */
    bool sticky; /* any lower dropped bit */
};

inline Shifted u128_shr_round(U128 a, uint32_t shift)
{
    if (shift == 0u) {
        return {.kept = a.lo, .half = false, .sticky = false};
    }
    /* the highest dropped bit is bit shift - 1 */
    const uint32_t half_bit = shift - 1u;
    const bool half =
        half_bit >= 64u ? ((a.hi >> (half_bit - 64u)) & 1u) != 0u : ((a.lo >> half_bit) & 1u) != 0u;
    bool sticky = false;
    if (half_bit >= 64u) {
        const uint64_t below_hi =
            half_bit == 64u ? uint64_t{0} : a.hi & ((uint64_t{1} << (half_bit - 64u)) - 1u);
        sticky = below_hi != 0u || a.lo != 0u;
    } else {
        const uint64_t below =
            half_bit == 0u ? uint64_t{0} : a.lo & ((uint64_t{1} << half_bit) - 1u);
        sticky = below != 0u;
    }
    uint64_t kept = 0u;
    if (shift >= 64u) {
        kept = a.hi >> (shift - 64u);
    } else {
        kept = (a.lo >> shift) | (a.hi << (64u - shift));
    }
    return {.kept = kept, .half = half, .sticky = sticky};
}

/* Round to nearest, ties to even. */
inline uint64_t round_kept(Shifted s)
{
    const bool up = s.half && (s.sticky || (s.kept & 1u) != 0u);
    return s.kept + (up ? uint64_t{1} : uint64_t{0});
}

/* ------------------------------------------------------------------ */
/* The operations                                                      */
/* ------------------------------------------------------------------ */

/* A positive normal fp32 value as a SoftDouble (exact). */
inline SoftDouble soft_from_float(float x)
{
    const auto bits = sycl::bit_cast<uint32_t>(x);
    const uint64_t mant = uint64_t{(bits & 0x007FFFFFu) | 0x00800000u} << 29;
    return {.mant = mant, .exp = (int32_t)((bits >> 23) & 0xFFu) - 150 - 29};
}

/* A positive integer below 2^32 as a SoftDouble (exact). */
inline SoftDouble soft_from_u32(uint32_t x)
{
    const uint32_t shift = 21u + (uint32_t)sycl::clz(x);
    return {.mant = (uint64_t)x << shift, .exp = -(int32_t)shift};
}

/* Round `mant` (below 2^56, three extra bits at the bottom, the last one
 * sticky) to 53 bits, ties to even. */
inline SoftDouble soft_round(uint64_t mant, int32_t exp)
{
    const uint64_t low = mant & 7u;
    uint64_t kept = mant >> 3;
    if (low > 4u || (low == 4u && (kept & 1u) != 0u)) {
        kept += 1u;
    }
    if (kept == (kDoubleTop << 1)) {
        return {.mant = kDoubleTop, .exp = exp + 4};
    }
    return {.mant = kept, .exp = exp + 3};
}

/* fl64(a + b) for positive a and b. */
inline SoftDouble soft_add(SoftDouble a, SoftDouble b)
{
    /* Scalar selects, not a select of the structs: a struct select stays in
     * private memory on the device (ADR-1395). */
    const bool swap = (b.exp > a.exp) || (b.exp == a.exp && b.mant > a.mant);
    const uint64_t big_mant = swap ? b.mant : a.mant;
    const uint64_t small_mant = swap ? a.mant : b.mant;
    const int32_t big_exp = swap ? b.exp : a.exp;
    const int32_t small_exp = swap ? a.exp : b.exp;
    /* Beyond 59 places the smaller term is below the last kept bit and only
     * sets the sticky bit; 59 gives the same sum. */
    const uint32_t distance = (uint32_t)(big_exp - small_exp);
    const uint32_t shift = distance < 59u ? distance : 59u;
    const uint64_t small_wide = small_mant << 3;
    const uint64_t lost = small_wide & ((uint64_t{1} << shift) - 1u);
    const uint64_t aligned = (small_wide >> shift) | (lost != 0u ? uint64_t{1} : uint64_t{0});
    uint64_t sum = (big_mant << 3) + aligned;
    int32_t exp = big_exp - 3;
    if (sum >= (kDoubleTop << 4)) {
        sum = (sum >> 1) | (sum & 1u);
        exp += 1;
    }
    return soft_round(sum, exp);
}

/* fl64(a / b) for positive a and b. Restoring division, one quotient bit per
 * step: 56 bits of quotient and the remainder as the sticky bit. */
inline SoftDouble soft_div(SoftDouble a, SoftDouble b)
{
    uint64_t rem = a.mant;
    int32_t exp = a.exp - b.exp - 55;
    if (rem < b.mant) {
        rem <<= 1;
        exp -= 1;
    }
    uint64_t quot = 0u;
    for (int step = 0; step < 56; step++) {
        const bool take = rem >= b.mant;
        rem -= take ? b.mant : uint64_t{0};
        quot = (quot << 1) | (take ? uint64_t{1} : uint64_t{0});
        rem <<= 1;
    }
    quot |= rem != 0u ? uint64_t{1} : uint64_t{0};
    return soft_round(quot, exp);
}

/* fl64(a * b). */
inline SoftDouble soft_mul(SoftDouble a, SoftDouble b)
{
    const U128 product = u128_mul(a.mant, b.mant);
    /* In [2^104, 2^106): 105 or 106 bits. */
    const uint32_t shift = (product.hi >> 41) != 0u ? 53u : 52u;
    uint64_t mant = round_kept(u128_shr_round(product, shift));
    int32_t exp = a.exp + b.exp + (int32_t)shift;
    if (mant == (kDoubleTop << 1)) {
        mant = kDoubleTop;
        exp += 1;
    }
    return {.mant = mant, .exp = exp};
}

inline bool soft_less(SoftDouble a, SoftDouble b)
{
    return a.exp < b.exp || (a.exp == b.exp && a.mant < b.mant);
}

/* (int64_t)value: truncation. The value is below 2^63. */
inline int64_t soft_trunc(SoftDouble a)
{
    if (a.exp >= 0) {
        return (int64_t)(a.mant << (uint32_t)a.exp);
    }
    const uint32_t shift = (uint32_t)(-a.exp);
    return shift >= 64u ? int64_t{0} : (int64_t)(a.mant >> shift);
}

/* (float)value, ties to even. The value is at least 1 and far below the
 * fp32 range's end here. */
inline float soft_to_float(SoftDouble value)
{
    const uint64_t low = value.mant & ((uint64_t{1} << 29) - 1u);
    const uint64_t half = uint64_t{1} << 28;
    uint64_t kept = value.mant >> 29;
    int32_t exp = value.exp + 29;
    if (low > half || (low == half && (kept & 1u) != 0u)) {
        kept += 1u;
    }
    if (kept == (uint64_t{1} << 24)) {
        kept >>= 1;
        exp += 1;
    }
    const auto biased = (uint32_t)(exp + 150);
    return sycl::bit_cast<float>((biased << 23) | ((uint32_t)kept & 0x007FFFFFu));
}

/* trunc(fl64(a - t)) for an integer a below 2^31 and 0 < t < a. */
inline uint32_t soft_sub_trunc(uint32_t a, SoftDouble t)
{
    if (t.exp >= 0) {
        /* t is an integer: the difference is exact. */
        return a - (uint32_t)(t.mant << (uint32_t)t.exp);
    }
    const uint32_t frac_bits = (uint32_t)(-t.exp); /* at most 84: t >= 2^-31 */
    const U128 exact = u128_sub(u128_shl(a, frac_bits), t.mant);
    const uint32_t msb = u128_msb(exact);
    /* The difference is exact * 2^-frac_bits. Round it to 53 bits. */
    const uint32_t drop = msb > 52u ? msb - 52u : 0u;
    const uint64_t mant = round_kept(u128_shr_round(exact, drop));
    /* value = mant * 2^(drop - frac_bits); mant may be 2^53 after rounding. */
    if (drop >= frac_bits) {
        return (uint32_t)(mant << (drop - frac_bits));
    }
    const uint32_t shift = frac_bits - drop;
    return shift >= 64u ? 0u : (uint32_t)(mant >> shift);
}

} // namespace vmaf_sycl_soft

#endif /* VMAF_FEATURE_SYCL_SYCL_SOFT_DOUBLE_H_ */
