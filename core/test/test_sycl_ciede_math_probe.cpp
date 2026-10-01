/**
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: EUPL-1.2
 *
 *  SYCL side of test_sycl_ciede_math (ADR-1436): the fp32 pair functions of
 *  feature/sycl/sycl_ff_math.h and the ciede2000 pixel of
 *  feature/sycl/sycl_ciede_math.h, callable from the C test on the host and
 *  on the device. core/test/meson.build compiles this TU with the SYCL
 *  feature line and links it through sycl_dependency, so the kernels below
 *  are built exactly like the extractor's. fp32 and integers only on the
 *  device (ADR-0220).
 */

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <new>
#include <optional>

#include <sycl/sycl.hpp>

#include "../src/feature/sycl/sycl_ciede_math.h"
#include "../src/feature/sycl/sycl_compat.h"
#include "../src/feature/sycl/sycl_ff_math.h"
#include "sycl_ciede_math_probe.h"

namespace
{

using vmaf_sycl_exact::Ff;
using vmaf_sycl_ffm::kAtanTable;
using vmaf_sycl_ffm::kAtanTableFloats;
using vmaf_sycl_ffm::kSinCosTable;
using vmaf_sycl_ffm::kSinCosTableFloats;
using vmaf_sycl_ffm::Tables;

/* Device allocation released on every exit, exceptions included. */
template <typename T> class DeviceBlock
{
  public:
    DeviceBlock(sycl::queue &queue, size_t count)
        : q_(queue), ptr_(sycl::malloc_device<T>(count == 0 ? 1 : count, queue))
    {
        if (ptr_ == nullptr) {
            throw std::bad_alloc();
        }
    }
    DeviceBlock(const DeviceBlock &) = delete;
    DeviceBlock &operator=(const DeviceBlock &) = delete;
    DeviceBlock(DeviceBlock &&) = delete;
    DeviceBlock &operator=(DeviceBlock &&) = delete;
    ~DeviceBlock()
    {
        sycl::free(ptr_, q_);
    }
    [[nodiscard]] T *get() const
    {
        return ptr_;
    }

  private:
    sycl::queue &q_;
    T *ptr_;
};

/* What one evaluation returns. */
struct FfResult {
    Ff value;
    int32_t exponent;
};

/* One evaluation of pair function `function`. Flattened so that a kernel
 * holding it has no call left (ADR-1395). */
__attribute__((flatten, always_inline)) inline FfResult evaluate(int function, float a, float b,
                                                                 const Tables &tables)
{
    const Ff pair = {.hi = a, .lo = b};
    switch (function) {
    case VMAF_TEST_FF_SQRT:
        return {.value = vmaf_sycl_ffm::sqrt(pair), .exponent = 0};
    case VMAF_TEST_FF_CBRT:
        return {.value = vmaf_sycl_ffm::cbrt(pair), .exponent = 0};
    case VMAF_TEST_FF_POW_2_4:
        return {.value = vmaf_sycl_ffm::pow_2_4(pair), .exponent = 0};
    case VMAF_TEST_FF_POW_7:
        return {.value = vmaf_sycl_ffm::pow_7(a), .exponent = 0};
    case VMAF_TEST_FF_EXP: {
        const vmaf_sycl_ffm::Exp e = vmaf_sycl_ffm::exp(a);
        return {.value = e.value, .exponent = e.k};
    }
    case VMAF_TEST_FF_SIN:
        return {.value = vmaf_sycl_ffm::sin_cos(pair, tables.sin_cos).sin, .exponent = 0};
    case VMAF_TEST_FF_COS:
        return {.value = vmaf_sycl_ffm::sin_cos(pair, tables.sin_cos).cos, .exponent = 0};
    default:
        return {.value = vmaf_sycl_ffm::atan2(a, b, tables.atan), .exponent = 0};
    }
}

/* The two tables in one device block, atan first. */
class DeviceTables
{
  public:
    explicit DeviceTables(sycl::queue &q) : block_(q, kAtanTableFloats + kSinCosTableFloats)
    {
        q.memcpy(block_.get(), kAtanTable, sizeof(kAtanTable));
        q.memcpy(block_.get() + kAtanTableFloats, kSinCosTable, sizeof(kSinCosTable));
        q.wait_and_throw();
    }
    [[nodiscard]] Tables get() const
    {
        return {.atan = block_.get(), .sin_cos = block_.get() + kAtanTableFloats};
    }

  private:
    DeviceBlock<float> block_;
};

void run_ff_device(sycl::queue &q, const VmafTestFfBatch &batch)
{
    const size_t n = batch.n;
    const DeviceTables device_tables(q);
    const Tables tables = device_tables.get();
    const DeviceBlock<float> floats(q, 4 * n);
    const DeviceBlock<int32_t> ints(q, n);
    float *a = floats.get();
    float *b = a + n;
    float *hi = a + 2 * n;
    float *lo = a + 3 * n;
    int32_t *exponent = ints.get();
    const int function = batch.function;
    q.memcpy(a, batch.a, n * sizeof(float));
    q.memcpy(b, batch.b, n * sizeof(float));
    q.wait_and_throw();
    q.parallel_for(sycl::range<1>(n), [=](sycl::id<1> i) {
        const FfResult r = evaluate(function, a[i], b[i], tables);
        hi[i] = r.value.hi;
        lo[i] = r.value.lo;
        exponent[i] = r.exponent;
    });
    q.wait_and_throw();
    q.memcpy(batch.hi, hi, n * sizeof(float));
    q.memcpy(batch.lo, lo, n * sizeof(float));
    q.memcpy(batch.exponent, exponent, n * sizeof(int32_t));
    q.wait_and_throw();
}

/* One pixel from its six samples, flattened like the extractor's. */
__attribute__((flatten, always_inline)) inline float
ciede_of(const float *s, const vmaf_sycl_ciede::Constants &k, const Tables &tables)
{
    return vmaf_sycl_ciede::pixel({.y = s[0], .u = s[1], .v = s[2]},
                                  {.y = s[3], .u = s[4], .v = s[5]}, k, tables);
}

void run_ciede_device(sycl::queue &q, const VmafTestCiedeBatch &batch)
{
    const size_t n = batch.n;
    const DeviceTables device_tables(q);
    const Tables tables = device_tables.get();
    const DeviceBlock<float> floats(q, 7 * n);
    float *samples = floats.get();
    float *delta_e = samples + 6 * n;
    const vmaf_sycl_ciede::Constants k = vmaf_sycl_ciede::make_constants(batch.bpc);
    q.memcpy(samples, batch.samples, 6 * n * sizeof(float));
    q.wait_and_throw();
    q.parallel_for(sycl::range<1>(n),
                   [=](sycl::id<1> i) { delta_e[i] = ciede_of(samples + 6 * i[0], k, tables); });
    q.wait_and_throw();
    q.memcpy(batch.delta_e, delta_e, n * sizeof(float));
    q.wait_and_throw();
}

/* Run `body` on a queue of the default GPU. */
template <typename Body> int on_default_gpu(Body body)
{
    std::optional<sycl::device> device;
    try {
        device.emplace(sycl::gpu_selector_v);
    } catch (const sycl::exception &) {
        return -ENODEV;
    }
    try {
        sycl::queue q(*device);
        body(q);
    } catch (const std::exception &) {
        return -EIO;
    }
    return 0;
}

} // namespace

extern "C" int vmaf_test_sycl_ff_batch(const VmafTestFfBatch *batch, int on_device)
{
    if (batch == nullptr || batch->a == nullptr || batch->b == nullptr || batch->hi == nullptr ||
        batch->lo == nullptr || batch->exponent == nullptr || batch->function < 0 ||
        batch->function >= VMAF_TEST_FF_COUNT) {
        return -EINVAL;
    }
    if (batch->n == 0) {
        return 0;
    }
    if (on_device != 0) {
        return on_default_gpu([batch](sycl::queue &q) { run_ff_device(q, *batch); });
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
    if (batch->n == 0) {
        return 0;
    }
    if (on_device != 0) {
        return on_default_gpu([batch](sycl::queue &q) { run_ciede_device(q, *batch); });
    }
    const Tables tables = {.atan = kAtanTable, .sin_cos = kSinCosTable};
    const vmaf_sycl_ciede::Constants k = vmaf_sycl_ciede::make_constants(batch->bpc);
    for (size_t i = 0; i < batch->n; i++) {
        batch->delta_e[i] = ciede_of(batch->samples + 6 * i, k, tables);
    }
    return 0;
}
