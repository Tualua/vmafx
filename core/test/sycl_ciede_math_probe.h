/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * The interface between test_sycl_ciede_math.c and its SYCL probe
 * (test_sycl_ciede_math_probe.cpp): the fp32 pair functions of
 * feature/sycl/sycl_ff_math.h and the ciede2000 pixel of
 * feature/sycl/sycl_ciede_math.h, callable from C on the host and on the
 * default GPU (ADR-1436).
 */

#ifndef LIBVMAF_TEST_SYCL_CIEDE_MATH_PROBE_H_
#define LIBVMAF_TEST_SYCL_CIEDE_MATH_PROBE_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The pair functions, by number. */
#define VMAF_TEST_FF_SQRT 0    /* sqrt(a + b) */
#define VMAF_TEST_FF_CBRT 1    /* cbrt(a + b) */
#define VMAF_TEST_FF_POW_2_4 2 /* (a + b)^2.4 */
#define VMAF_TEST_FF_POW_7 3   /* a^7 */
#define VMAF_TEST_FF_EXP 4     /* e^a = result * 2^k */
#define VMAF_TEST_FF_SIN 5     /* sin(a + b) */
#define VMAF_TEST_FF_COS 6     /* cos(a + b) */
#define VMAF_TEST_FF_ATAN2 7   /* atan2(a, b): the angle of the point (b, a) */
#define VMAF_TEST_FF_COUNT 8

#ifndef __cplusplus
typedef struct VmafTestFfBatch VmafTestFfBatch;
typedef struct VmafTestCiedeBatch VmafTestCiedeBatch;
#endif

/* n evaluations of one pair function. */
struct VmafTestFfBatch {
    int function;   /* VMAF_TEST_FF_* */
    const float *a; /* first operand, or the high word of a pair operand */
    const float *b; /* second operand, or the low word */
    size_t n;
    float *hi; /* out: the result pair */
    float *lo;
    int32_t *exponent; /* out: k of VMAF_TEST_FF_EXP, 0 otherwise */
};

/* On the host when `on_device` is 0, else in a kernel on the default GPU.
 * 0, -EINVAL, -ENODEV without a device, -EIO on a SYCL error. */
int vmaf_test_sycl_ff_batch(const VmafTestFfBatch *batch, int on_device);

/* n ciede2000 pixels of one bit depth. */
struct VmafTestCiedeBatch {
    const float *samples; /* 6 per pixel: reference y, u, v, distorted y, u, v */
    size_t n;
    unsigned bpc;
    float *delta_e; /* out */
};

/* On the host when `on_device` is 0, else in a kernel on the default GPU,
 * built as the extractor's is. 0, -EINVAL, -ENODEV, -EIO. */
int vmaf_test_sycl_ciede_batch(const VmafTestCiedeBatch *batch, int on_device);

#ifdef __cplusplus
}
#endif

#endif /* LIBVMAF_TEST_SYCL_CIEDE_MATH_PROBE_H_ */
