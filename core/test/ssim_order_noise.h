/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * Seeded noise for the SSIM frame-sum order cases (ADR-1463, ADR-1466).
 *
 * iqa_ssim() adds one double per window in raster order, and those adds
 * round. A twin that adds the same terms in another order returns another
 * float mean on the frames whose mean lies next to a rounding boundary, which
 * a search over independent noise pairs finds. A test names such a pair by
 * its seed and regenerates it here, so no picture has to be stored.
 *
 * Shared by test_sycl_float_ssim_parity.c and test_sycl_ms_ssim_parity.c.
 */

#ifndef VMAF_TEST_SSIM_ORDER_NOISE_H_
#define VMAF_TEST_SSIM_ORDER_NOISE_H_

#include <stddef.h>
#include <stdint.h>

/* splitmix64's output function. */
static inline uint64_t ssim_order_mix64(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

/* 8-bit luma sample `i` (raster order) of the reference (`which` 0) or the
 * distorted (1) picture of the pair `seed` names. */
static inline unsigned ssim_order_noise_luma(uint64_t seed, unsigned which, size_t i)
{
    return (unsigned)(ssim_order_mix64(ssim_order_mix64(seed * 2u + which) + i) >> 56);
}

#endif /* VMAF_TEST_SSIM_ORDER_NOISE_H_ */
