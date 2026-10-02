/**
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: EUPL-1.2
 *
 *  ciede2000 for SYCL kernels: feature/ciede_ff_math.h, the per-pixel
 *  arithmetic of ciede.c in fp32 pairs (ADR-1436), on the SYCL primitives of
 *  sycl_ff_math.h. The arithmetic is in the shared header, which the HIP twin
 *  includes too; this one adds the two primitives it needs beyond
 *  sycl_ff_math.h: the device's ldexp(), and the correctly rounded division
 *  of sycl_exact_fp.h, which the shared header takes from `vmaf_ffm_base`.
 *
 *  Kernel code may use everything in the shared header except make_pair()
 *  and make_constants(), which are host code and use fp64. A translation unit
 *  that includes this header is compiled with contraction off, as every SYCL
 *  feature TU is (ADR-1367).
 */

#ifndef VMAF_FEATURE_SYCL_SYCL_CIEDE_MATH_H_
#define VMAF_FEATURE_SYCL_SYCL_CIEDE_MATH_H_

#include <sycl/sycl.hpp>

#include "sycl_exact_fp.h"
#include "sycl_ff_math.h"

#define VMAF_FF_LDEXP(x, k) sycl::ldexp(x, k)

#include "../ciede_ff_math.h"

namespace vmaf_sycl_ciede = vmaf_ciede_ff;

#endif /* VMAF_FEATURE_SYCL_SYCL_CIEDE_MATH_H_ */
