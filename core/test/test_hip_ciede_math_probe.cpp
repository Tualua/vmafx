/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * ADR-1448 — the host half of sycl_ciede_math_probe.h for the HIP twin:
 * ciede_hip's per-pixel arithmetic (feature/ciede_ff_math.h on the primitives
 * of feature/hip/integer_ciede/ciede_hip_math.h), compiled by the host
 * compiler with the C library behind the primitives. test_sycl_ciede_math.c,
 * built as test_hip_ciede_math, holds the pair functions to the host's
 * extended-precision math library and the pixel to the reference's fp64
 * statements with it. No device: the kernels run the same statements, and
 * test_hip_ciede_parity compares their scores on one.
 *
 * Compiled with contraction off, like the kernels.
 */

#include <cerrno>
#include <cstddef>
#include <cstdint>

#include "feature/hip/integer_ciede/ciede_hip_math.h"
#include "sycl_ciede_math_probe.h"

namespace
{

using vmaf_ffm_base::Ff;
using vmaf_hip_ffm::kAtanTable;
using vmaf_hip_ffm::kSinCosTable;
using vmaf_hip_ffm::Tables;

/* What one evaluation returns. */
struct FfResult {
    Ff value;
    int32_t exponent;
};

/* One evaluation of pair function `function`. */
FfResult evaluate(int function, float a, float b, const Tables &tables)
{
    const Ff pair = {.hi = a, .lo = b};
    switch (function) {
    case VMAF_TEST_FF_SQRT:
        return {.value = vmaf_hip_ffm::sqrt(pair), .exponent = 0};
    case VMAF_TEST_FF_CBRT:
        return {.value = vmaf_hip_ffm::cbrt(pair), .exponent = 0};
    case VMAF_TEST_FF_POW_2_4:
        return {.value = vmaf_hip_ffm::pow_2_4(pair), .exponent = 0};
    case VMAF_TEST_FF_POW_7:
        return {.value = vmaf_hip_ffm::pow_7(a), .exponent = 0};
    case VMAF_TEST_FF_EXP: {
        const vmaf_hip_ffm::Exp e = vmaf_hip_ffm::exp(a);
        return {.value = e.value, .exponent = e.k};
    }
    case VMAF_TEST_FF_SIN:
        return {.value = vmaf_hip_ffm::sin_cos(pair, tables.sin_cos).sin, .exponent = 0};
    case VMAF_TEST_FF_COS:
        return {.value = vmaf_hip_ffm::sin_cos(pair, tables.sin_cos).cos, .exponent = 0};
    default:
        return {.value = vmaf_hip_ffm::atan2(a, b, tables.atan), .exponent = 0};
    }
}

} // namespace

extern "C" int vmaf_test_sycl_ff_batch(const VmafTestFfBatch *batch, int on_device)
{
    if (batch == nullptr || batch->a == nullptr || batch->b == nullptr || batch->hi == nullptr ||
        batch->lo == nullptr || batch->exponent == nullptr || batch->function < 0 ||
        batch->function >= VMAF_TEST_FF_COUNT) {
        return -EINVAL;
    }
    if (on_device != 0) {
        return -ENODEV;
    }
    const Tables tables = {.atan = kAtanTable, .sin_cos = kSinCosTable};
    for (size_t i = 0; i < batch->n; i++) {
        const FfResult r = evaluate(batch->function, batch->a[i], batch->b[i], tables);
        batch->hi[i] = r.value.hi;
        batch->lo[i] = r.value.lo;
        batch->exponent[i] = r.exponent;
    }
    return 0;
}

extern "C" int vmaf_test_sycl_ciede_batch(const VmafTestCiedeBatch *batch, int on_device)
{
    if (batch == nullptr || batch->samples == nullptr || batch->delta_e == nullptr ||
        batch->bpc < 8u || batch->bpc > 16u) {
        return -EINVAL;
    }
    if (on_device != 0) {
        return -ENODEV;
    }
    const Tables tables = {.atan = kAtanTable, .sin_cos = kSinCosTable};
    const vmaf_hip_ciede::Constants k = vmaf_hip_ciede::make_constants(batch->bpc);
    for (size_t i = 0; i < batch->n; i++) {
        const float *s = batch->samples + 6 * i;
        batch->delta_e[i] = vmaf_hip_ciede::pixel({.y = s[0], .u = s[1], .v = s[2]},
                                                  {.y = s[3], .u = s[4], .v = s[5]}, k, tables);
    }
    return 0;
}
