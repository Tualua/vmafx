/**
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: EUPL-1.2
 *
 *  The fp32 pair functions of feature/ff_math.h for SYCL kernels (ADR-0220:
 *  no fp64 type on the device). The functions, the constants and the tables
 *  are in that header, shared with the HIP twin; this one names the SYCL
 *  primitives they are built on:
 *
 *    - the pair type and the exact pair operations of sycl_exact_fp.h;
 *    - the device's own sqrt(), cbrt() and pow() as the root estimates the
 *      functions correct, and rint() and fabs().
 *
 *  Every function is VMAF_SYCL_ALWAYS_INLINE: a call inside a kernel takes
 *  its frame from scratch memory (ADR-1395). A caller copies kAtanTable and
 *  kSinCosTable to device memory and hands sin_cos() and atan2() pointers
 *  into it, for the same reason. This header must not mention the fp64 type.
 */

#ifndef VMAF_FEATURE_SYCL_SYCL_FF_MATH_H_
#define VMAF_FEATURE_SYCL_SYCL_FF_MATH_H_

#include <sycl/sycl.hpp>

#include <cstdint>

#include "sycl_compat.h"
#include "sycl_exact_fp.h"

namespace vmaf_ffm_base = vmaf_sycl_exact;

#define VMAF_FF_INLINE VMAF_SYCL_ALWAYS_INLINE
#define VMAF_FF_FABS(x) sycl::fabs(x)
#define VMAF_FF_RINT(x) sycl::rint(x)
#define VMAF_FF_SQRT(x) sycl::sqrt(x)
#define VMAF_FF_CBRT(x) sycl::cbrt(x)
#define VMAF_FF_ROOT5(x) sycl::pow(x, 0.2f)

#include "../ff_math.h"

namespace vmaf_sycl_ffm = vmaf_ffm;

#endif /* VMAF_FEATURE_SYCL_SYCL_FF_MATH_H_ */
