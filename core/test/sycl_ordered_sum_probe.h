/**
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: EUPL-1.2
 *
 *  Interface between test_sycl_ordered_sum.c and its probe
 *  (test_sycl_ordered_sum_probe.cpp, compiled as SYCL): the ordered sum of
 *  feature/sycl/sycl_ordered_sum.h over an array of fp64 bit patterns, with
 *  the plan's advice under the test's control.
 */

#ifndef LIBVMAF_TEST_SYCL_ORDERED_SUM_PROBE_H_
#define LIBVMAF_TEST_SYCL_ORDERED_SUM_PROBE_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __cplusplus
typedef struct VmafTestOrdsumCase VmafTestOrdsumCase;
#endif

/* What the plan is given as advice for each chunk. */
/* The chunk's fp32 sum times `advice_factor`. */
#define VMAF_TEST_ORDSUM_ADVICE_SUMS 0
/* Zero: "every term of the chunk is zero". */
#define VMAF_TEST_ORDSUM_ADVICE_ZERO 1
/* `advice_factor` for every chunk. */
#define VMAF_TEST_ORDSUM_ADVICE_CONSTANT 2
/* A NaN. */
#define VMAF_TEST_ORDSUM_ADVICE_NAN 3

struct VmafTestOrdsumCase {
    const uint64_t *terms; /* fp64 bit patterns, non-negative or NaN */
    size_t count;
    int advice;          /* VMAF_TEST_ORDSUM_ADVICE_* */
    float advice_factor; /* 1 is honest advice */
    int scale_log2;      /* the advice sums are sums of term * 2^scale_log2 */
    unsigned slot_limit; /* chunks that may keep their terms and runs */
    int on_device;       /* 0: the walk runs on the host; else in a kernel on the default GPU */
    uint64_t sum;        /* out: the sum's bit pattern */
    size_t asked_terms;  /* out: terms the walk asked for (chunks without a slot); host only */
};

/* 0, -EINVAL, -ENODEV without a device, -EIO on a SYCL error. */
int vmaf_test_sycl_ordsum(VmafTestOrdsumCase *c);

#ifdef __cplusplus
}
#endif

#endif /* LIBVMAF_TEST_SYCL_ORDERED_SUM_PROBE_H_ */
