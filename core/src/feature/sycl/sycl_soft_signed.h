/**
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: EUPL-1.2
 *
 *  Signed fp64 values in 64-bit integers, for a SYCL kernel that has to
 *  return the `double` a CPU reference computes (ADR-0220: a kernel has no
 *  fp64 type). sycl_soft_double.h has the positive values and their
 *  product, quotient and sum; this header adds the sign, zero, the difference
 *  (with its cancellation) and the conversions from an integer and to the
 *  IEEE-754 bit pattern. Every operation rounds to nearest, ties to even, as
 *  the fp64 operation it stands for, so a sequence of them is the reference's
 *  sequence bit for bit.
 *
 *  Range: normal values and zero. No subnormal, infinity or NaN is formed or
 *  accepted; a caller's values stay far inside the fp64 range.
 *
 *  Kernel-safe: integers only, no arrays, and scalar selects (a select
 *  between structs stays in private memory on the device, ADR-1395). Every
 *  function is always inlined: a call left in a kernel takes scratch memory
 *  for its frame.
 */

#ifndef VMAF_FEATURE_SYCL_SYCL_SOFT_SIGNED_H_
#define VMAF_FEATURE_SYCL_SYCL_SOFT_SIGNED_H_

#include <sycl/sycl.hpp>

#include <cstdint>

#include "sycl_compat.h"
#include "sycl_soft_double.h"

namespace vmaf_sycl_soft
{

/* An fp64 value: (negative ? -1 : 1) * mant * 2^exp with mant in
 * [2^52, 2^53), or zero (mant 0, exp kZeroExp, not negative). */
struct SoftSigned {
    uint64_t mant;
    int32_t exp;
    uint32_t negative;
};

/* Zero's exponent: below every value's, so zero is the smaller operand of
 * any sum without a test for it. */
inline constexpr int32_t kZeroExp = -(int32_t{1} << 20);

inline constexpr uint64_t kFractionMask = kDoubleTop - 1u;
/* The bit pattern of a quiet NaN, for a caller that has to hand on "not a
 * number": no operation here forms or accepts one. */
inline constexpr uint64_t kQuietNanBits = 0x7FF8000000000000u;
inline constexpr int32_t kExponentBias = 1023;
inline constexpr uint32_t kExponentFieldMask = 0x7FFu;
inline constexpr int32_t kMantissaBits = 52;
/* The guard bits a sum carries below the significand: soft_round()'s three. */
inline constexpr uint32_t kGuardBits = 3u;
/* The highest bit of a significand with its guard bits. */
inline constexpr uint32_t kGuardedTopBit = 55u;

VMAF_SYCL_ALWAYS_INLINE SoftSigned signed_make(uint64_t mant, int32_t exp, bool negative)
{
    const bool zero = mant == 0u;
    return {.mant = mant, .exp = zero ? kZeroExp : exp, .negative = (!zero && negative) ? 1u : 0u};
}

/* fl64(value) for an unsigned integer: exact below 2^53, rounded above. */
VMAF_SYCL_ALWAYS_INLINE SoftSigned signed_from_u64(uint64_t value)
{
    const auto lead = (uint32_t)sycl::clz(value | uint64_t{1});
    /* The highest set bit is bit 63 - lead; the significand's is bit 52. */
    const bool wide = lead < 11u;
    const uint32_t drop = wide ? 11u - lead : 0u;
    const uint32_t lift = wide ? 0u : lead - 11u;
    const uint32_t half_bit = wide ? drop - 1u : 0u;
    const bool half = wide && ((value >> half_bit) & 1u) != 0u;
    const bool sticky = wide && (value & ((uint64_t{1} << half_bit) - 1u)) != 0u;
    uint64_t kept = wide ? value >> drop : value << lift;
    if (half && (sticky || (kept & 1u) != 0u)) {
        kept += 1u;
    }
    int32_t exp = wide ? (int32_t)drop : -(int32_t)lift;
    if (kept == (kDoubleTop << 1)) {
        kept = kDoubleTop;
        exp += 1;
    }
    return signed_make(value == 0u ? uint64_t{0} : kept, exp, false);
}

/* An unsigned integer below 2^53 as the fp64 value it is: no rounding. */
VMAF_SYCL_ALWAYS_INLINE SoftSigned signed_from_exact(uint64_t value)
{
    const uint32_t lift = (uint32_t)sycl::clz(value | uint64_t{1}) - 11u;
    return signed_make(value << lift, -(int32_t)lift, false);
}

/* A finite fp32 value as the fp64 value it is: exact, subnormals included. */
VMAF_SYCL_ALWAYS_INLINE SoftSigned signed_from_float(float x)
{
    const auto bits = sycl::bit_cast<uint32_t>(x);
    const uint32_t field = (bits >> 23) & 0xFFu;
    const uint64_t fraction = bits & 0x007FFFFFu;
    /* A normal value is (2^23 + fraction) * 2^(field - 150), a subnormal one
     * fraction * 2^-149. */
    const uint64_t integer = field == 0u ? fraction : (fraction | uint64_t{0x00800000});
    const int32_t scale = field == 0u ? -149 : (int32_t)field - 150;
    const SoftSigned magnitude = signed_from_exact(integer);
    return signed_make(magnitude.mant, magnitude.exp + scale, (bits >> 31) != 0u);
}

/* The value of an fp64 bit pattern: a normal value or zero. */
VMAF_SYCL_ALWAYS_INLINE SoftSigned signed_from_bits(uint64_t bits)
{
    const auto field = (uint32_t)(bits >> kMantissaBits) & kExponentFieldMask;
    const uint64_t mant = field == 0u ? uint64_t{0} : (bits & kFractionMask) | kDoubleTop;
    return signed_make(mant, (int32_t)field - kExponentBias - kMantissaBits, (bits >> 63) != 0u);
}

/* The fp64 bit pattern of a value. */
VMAF_SYCL_ALWAYS_INLINE uint64_t signed_bits(SoftSigned a)
{
    const auto field = (uint64_t)(uint32_t)(a.exp + kMantissaBits + kExponentBias);
    const uint64_t magnitude = (field << kMantissaBits) | (a.mant & kFractionMask);
    const uint64_t sign = (uint64_t)a.negative << 63;
    return a.mant == 0u ? uint64_t{0} : sign | magnitude;
}

VMAF_SYCL_ALWAYS_INLINE SoftSigned signed_negate(SoftSigned a)
{
    return signed_make(a.mant, a.exp, a.negative == 0u);
}

/* |a|. */
VMAF_SYCL_ALWAYS_INLINE SoftSigned signed_abs(SoftSigned a)
{
    return signed_make(a.mant, a.exp, false);
}

/* 2 * a: exact. */
VMAF_SYCL_ALWAYS_INLINE SoftSigned signed_twice(SoftSigned a)
{
    return signed_make(a.mant, a.exp + 1, a.negative != 0u);
}

/* fl64(a + b).
 *
 * The smaller operand is aligned under the larger with three guard bits, the
 * bits shifted out kept as one sticky bit in the lowest place. For a sum that
 * is soft_add(). For a difference: when the exponents are at most one apart
 * nothing is shifted out and the difference is exact, however many leading
 * bits cancel; when they are further apart the difference keeps at least half
 * of the larger operand, so it is normalised by one place at most and the
 * sticky bit stays below the rounding bit. */
VMAF_SYCL_ALWAYS_INLINE SoftSigned signed_add(SoftSigned a, SoftSigned b)
{
    const bool swap = (b.exp > a.exp) || (b.exp == a.exp && b.mant > a.mant);
    const uint64_t big_mant = swap ? b.mant : a.mant;
    const uint64_t small_mant = swap ? a.mant : b.mant;
    const int32_t big_exp = swap ? b.exp : a.exp;
    const int32_t small_exp = swap ? a.exp : b.exp;
    const bool big_negative = (swap ? b.negative : a.negative) != 0u;
    const bool subtract = a.negative != b.negative;
    /* Beyond 59 places the smaller operand is below the last guard bit and
     * only sets the sticky bit; 59 gives the same result. */
    const auto distance = (uint32_t)(big_exp - small_exp);
    const uint32_t shift = distance < 59u ? distance : 59u;
    const uint64_t small_wide = small_mant << kGuardBits;
    const uint64_t lost = small_wide & ((uint64_t{1} << shift) - 1u);
    const uint64_t aligned = (small_wide >> shift) | (lost != 0u ? uint64_t{1} : uint64_t{0});
    const uint64_t big_wide = big_mant << kGuardBits;

    uint64_t sum = big_wide + aligned;
    int32_t sum_exp = big_exp - (int32_t)kGuardBits;
    if (sum >= (kDoubleTop << (kGuardBits + 1u))) {
        sum = (sum >> 1) | (sum & 1u);
        sum_exp += 1;
    }

    const uint64_t difference = big_wide - aligned;
    const uint32_t lead = (uint32_t)sycl::clz(difference | uint64_t{1});
    const uint32_t lift = lead - (63u - kGuardedTopBit);
    const uint64_t difference_wide = difference << lift;
    const int32_t difference_exp = big_exp - (int32_t)kGuardBits - (int32_t)lift;

    const SoftDouble rounded =
        soft_round(subtract ? difference_wide : sum, subtract ? difference_exp : sum_exp);
    const bool zero = subtract ? difference == 0u : big_mant == 0u;
    return signed_make(zero ? uint64_t{0} : rounded.mant, rounded.exp, big_negative);
}

/* fl64(a - b). */
VMAF_SYCL_ALWAYS_INLINE SoftSigned signed_sub(SoftSigned a, SoftSigned b)
{
    return signed_add(a, signed_negate(b));
}

/* fl64(a * b). */
VMAF_SYCL_ALWAYS_INLINE SoftSigned signed_mul(SoftSigned a, SoftSigned b)
{
    const SoftDouble product =
        soft_mul({.mant = a.mant, .exp = a.exp}, {.mant = b.mant, .exp = b.exp});
    const bool zero = a.mant == 0u || b.mant == 0u;
    return signed_make(zero ? uint64_t{0} : product.mant, product.exp, a.negative != b.negative);
}

/* One step of a long division in radix 2^19: the next 19 quotient bits of
 * rem / den and the remainder they leave, for rem < den and den in
 * [2^52, 2^53).
 *
 * The digit is estimated in fp32 from the top 24 bits of each operand, which
 * fp32 holds exactly. The estimate is within one of the digit: the truncated
 * operands move the quotient by less than 2^-22 and the fp32 division by less
 * than 2^-22 more, a quarter of a digit together, and the conversion to an
 * integer truncates. The remainder is then exact integer arithmetic: it lies
 * within three divisors of zero, so its low 64 bits are its value, and two
 * corrections each way put the digit right whichever way the device's
 * division rounds. */
struct DivStep {
    uint64_t rem;
    uint64_t digit;
};

inline constexpr uint32_t kDigitBits = 19u;
inline constexpr float kDigitScale = 524288.0f;
/* The bits of a significand below its top 24. */
inline constexpr uint32_t kBelowFloat = 29u;

VMAF_SYCL_ALWAYS_INLINE DivStep div_step(uint64_t rem, uint64_t den)
{
    const float estimate =
        (float)(uint32_t)(rem >> kBelowFloat) / (float)(uint32_t)(den >> kBelowFloat);
    uint64_t digit = (uint64_t)(uint32_t)(estimate * kDigitScale);
    auto left = (int64_t)((rem << kDigitBits) - digit * den);
    const auto divisor = (int64_t)den;
    for (int correction = 0; correction < 2; correction++) {
        const bool below = left < 0;
        left += below ? divisor : int64_t{0};
        digit -= below ? uint64_t{1} : uint64_t{0};
    }
    for (int correction = 0; correction < 2; correction++) {
        const bool above = left >= divisor;
        left -= above ? divisor : int64_t{0};
        digit += above ? uint64_t{1} : uint64_t{0};
    }
    return {.rem = (uint64_t)left, .digit = digit};
}

/* fl64(a / b) for positive a and b: soft_div()'s result from three radix-2^19
 * steps instead of 56 one-bit steps. The quotient's first bit and 57 more are
 * formed; the two lowest and the remainder are the sticky bit. */
VMAF_SYCL_ALWAYS_INLINE SoftDouble soft_div_digits(SoftDouble a, SoftDouble b)
{
    uint64_t rem = a.mant;
    int32_t exp = a.exp - b.exp - 55;
    if (rem < b.mant) {
        rem <<= 1;
        exp -= 1;
    }
    /* rem is in [b.mant, 2 * b.mant): the first quotient bit is one. */
    rem -= b.mant;
    uint64_t quot = 1u;
    for (int step = 0; step < 3; step++) {
        const DivStep next = div_step(rem, b.mant);
        quot = (quot << kDigitBits) | next.digit;
        rem = next.rem;
    }
    const bool sticky = (quot & 3u) != 0u || rem != 0u;
    return soft_round((quot >> 2) | (sticky ? uint64_t{1} : uint64_t{0}), exp);
}

/* fl64(a / b); b is not zero. */
VMAF_SYCL_ALWAYS_INLINE SoftSigned signed_div(SoftSigned a, SoftSigned b)
{
    const SoftDouble quotient =
        soft_div_digits({.mant = a.mant, .exp = a.exp}, {.mant = b.mant, .exp = b.exp});
    return signed_make(a.mant == 0u ? uint64_t{0} : quotient.mant, quotient.exp,
                       a.negative != b.negative);
}

} // namespace vmaf_sycl_soft

#endif /* VMAF_FEATURE_SYCL_SYCL_SOFT_SIGNED_H_ */
