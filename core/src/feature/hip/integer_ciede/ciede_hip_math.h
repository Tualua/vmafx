/**
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: EUPL-1.2
 *
 *  ciede2000 for HIP kernels: feature/ciede_ff_math.h, the per-pixel
 *  arithmetic of ciede.c in fp32 pairs (the SYCL twin's, ADR-1436), on the
 *  primitives of a HIP device (ADR-1448). The arithmetic and the pair
 *  functions are in the shared headers; this one names what they are built
 *  on here:
 *
 *    - the exact pair operations of feature/ff_pair.h: fp32 `+ - * /` are
 *      IEEE operations under the strict FP list of core/src/meson.build
 *      (-ffp-contract=off, -fhip-fp32-correctly-rounded-divide-sqrt), and
 *      fmaf() is one rounding by definition;
 *    - sqrtf(), cbrtf() and exp(log(x) / 5) as the root estimates the pair
 *      functions correct. An estimate within 2^-16 of the root is enough,
 *      which fp32 functions give with room. The fifth root is not powf(x,
 *      0.2f): the device's powf() is as good and takes 7 of 50 ms per
 *      1920x1080 frame on a gfx1036.
 *
 *  Under hipcc every function of the shared headers is callable from a
 *  kernel and from the compiler's constant evaluation. The host compiler
 *  takes the same header for the device-free replay
 *  (core/test/test_hip_ciede_math_probe.cpp), with the C library behind the
 *  primitives. A translation unit that includes it is compiled with
 *  contraction off and as C++20.
 */

#ifndef VMAF_FEATURE_HIP_INTEGER_CIEDE_CIEDE_HIP_MATH_H_
#define VMAF_FEATURE_HIP_INTEGER_CIEDE_CIEDE_HIP_MATH_H_

#if !defined(__HIPCC__)
#include <math.h>
#endif

#define VMAF_FF_INLINE inline __attribute__((always_inline))
#define VMAF_FF_FMA(a, b, c) fmaf((a), (b), (c))
#define VMAF_FF_FABS(x) fabsf(x)
#define VMAF_FF_RINT(x) rintf(x)
#define VMAF_FF_SQRT(x) sqrtf(x)
#define VMAF_FF_CBRT(x) cbrtf(x)
#define VMAF_FF_ROOT5(x) expf(0.2f * logf(x))
#define VMAF_FF_LDEXP(x, k) ldexpf((x), (k))

#if defined(__HIPCC__)
#pragma clang force_cuda_host_device begin
#endif

#include "feature/ff_pair.h"

namespace vmaf_ffm_base = vmaf_ff_pair;

#include "feature/ciede_ff_math.h"

#if defined(__HIPCC__)
#pragma clang force_cuda_host_device end
#endif

namespace vmaf_hip_ciede = vmaf_ciede_ff;
namespace vmaf_hip_ffm = vmaf_ffm;

#endif /* VMAF_FEATURE_HIP_INTEGER_CIEDE_CIEDE_HIP_MATH_H_ */
