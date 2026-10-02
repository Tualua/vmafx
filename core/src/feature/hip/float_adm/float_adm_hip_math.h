/**
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: EUPL-1.2
 *
 *  float_adm for HIP kernels: the spelling HIP device code needs for the
 *  arithmetic of feature/float_adm_gpu_common.h, the header the CUDA twin
 *  runs (ADR-1458). The kernels (float_adm_score.hip) and the device probe
 *  of core/test/test_hip_float_adm_math compile the arithmetic through this
 *  header.
 *
 *  The shared header's rounding operations stay its defaults here, the plain
 *  C operators. They are the reference's operations because every HIP kernel
 *  is built with the strict FP list of core/src/meson.build (ADR-1407): no
 *  contraction, and an fp32 `/` that is the correctly rounded quotient, which
 *  is what the reference's DIVS() is since ADR-1442. The fp64 product and sum
 *  are IEEE operations on the device. Do not map them to the `__fmul_rn()`
 *  family: on HIP those are plain operators unless
 *  OCML_BASIC_ROUNDED_OPERATIONS is defined, and say nothing the flags do
 *  not.
 *
 *  Not for host code: a host TU includes feature/float_adm_gpu_common.h
 *  directly.
 */

#ifndef VMAF_SRC_FEATURE_HIP_FLOAT_ADM_FLOAT_ADM_HIP_MATH_H_
#define VMAF_SRC_FEATURE_HIP_FLOAT_ADM_FLOAT_ADM_HIP_MATH_H_

#define FADM_HD __device__ __forceinline__
#define FADM_BITS(x) __float_as_uint(x)
#define FADM_FROM_BITS(u) __uint_as_float(u)
/* The device's powf(x, 1.0f) is not x for every x (measured on a gfx1036),
 * and the C library's is: adm_p_norm = 1 stays exact with the identity. */
#define FADM_POWF(x, p) (((p) == 1.0f) ? (x) : powf((x), (p)))
#define FADM_DEVICE_ONLY

#include "feature/float_adm_gpu_common.h"

#endif /* VMAF_SRC_FEATURE_HIP_FLOAT_ADM_FLOAT_ADM_HIP_MATH_H_ */
