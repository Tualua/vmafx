/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * Residual variance of integer VIF's gain model, one definition for the
 * scalar statistic (integer_vif.c), its AVX2 and NEON per-pixel twins, and the
 * CUDA and HIP kernels (ADR-1561).
 *
 * Netflix's integer_vif.c writes
 *
 *     int32_t sv_sq = sigma2_sq - g * sigma12;
 *     sv_sq = (uint32_t)(MAX(sv_sq, 0));
 *
 * The fp64 difference reaches about -2^45 (g = sigma12 / sigma1_sq with
 * sigma1_sq >= 2^17 and sigma12 < 2^31), and converting a value outside
 * int32_t's range is undefined (C11 6.3.1.4, C++ [conv.fpint]). x86's
 * cvttsd2si and cvttpd2dq return INT32_MIN for such a value, which the clamp
 * turns into 0. An aarch64 build that vectorises the line converts through
 * 64-bit lanes (fcvtzs on .2d) and keeps the low 32 bits, which can be
 * positive. vif_sv_sq() returns x86's value for every input: the difference
 * truncated toward zero when it lies in (0, 2^31), 0 otherwise.
 *
 * Kernels set VMAF_IVIF_FUNC to their device qualifiers before including this
 * header. The difference stays one fp64 product and one fp64 subtraction:
 * every translation unit that includes it is built with contraction off
 * (ADR-1461, ADR-1403).
 */

#ifndef VMAF_FEATURE_INTEGER_VIF_SV_SQ_H_
#define VMAF_FEATURE_INTEGER_VIF_SV_SQ_H_

#include <stdint.h>

#ifndef VMAF_IVIF_FUNC
#define VMAF_IVIF_FUNC static inline
#endif

VMAF_IVIF_FUNC uint32_t vif_sv_sq(int32_t sigma2_sq, double g, int32_t sigma12)
{
    const double sv = sigma2_sq - g * sigma12;
    return (sv > 0.0 && sv < 2147483648.0) ? (uint32_t)sv : 0u;
}

#endif /* VMAF_FEATURE_INTEGER_VIF_SV_SQ_H_ */
