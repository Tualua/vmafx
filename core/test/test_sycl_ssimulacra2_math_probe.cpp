/**
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: EUPL-1.2
 *
 *  SYCL side of test_sycl_ssimulacra2_math (ADR-1446): the per-pixel terms of
 *  feature/sycl/sycl_ssimulacra2_math.h, callable from the C test on the host
 *  and on the device. core/test/meson.build compiles this TU with the SYCL
 *  feature line and links it through sycl_dependency, so the kernel below is
 *  built exactly like the extractor's. Integers and fp32 only on the device
 *  (ADR-0220).
 */

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <new>
#include <optional>

#include <sycl/sycl.hpp>

#include "../src/feature/sycl/sycl_compat.h"
#include "../src/feature/sycl/sycl_ssimulacra2_math.h"

namespace
{

/* fp32 inputs per sample: num_m, num_s, denom_s, r1, m1, r2, m2. */
constexpr size_t INPUTS = 7;
/* fp64 bit patterns per sample: d, d^4, artifact, artifact^4, detail,
 * detail^4, the order of the extractor's six sums. */
constexpr size_t OUTPUTS = 6;
/* The shape of the extractor's unit kernels. */
constexpr int PROBE_SG = 16;
constexpr int PROBE_GRF = 0;

__attribute__((flatten, always_inline)) inline void terms_of(const float *in, uint64_t *out)
{
    const vmaf_sycl_ss2::TermPair d = vmaf_sycl_ss2::ssim_terms(in[0], in[1], in[2]);
    const vmaf_sycl_ss2::EdgeTerms e = vmaf_sycl_ss2::edge_terms(in[3], in[4], in[5], in[6]);
    out[0] = d.value;
    out[1] = d.fourth;
    out[2] = e.artifact.value;
    out[3] = e.artifact.fourth;
    out[4] = e.detail.value;
    out[5] = e.detail.fourth;
}

/* Device allocation released on every exit, exceptions included. */
template <typename T> class DeviceBlock
{
  public:
    DeviceBlock(sycl::queue &queue, size_t count)
        : q_(queue), ptr_(sycl::malloc_device<T>(count, queue))
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

class TermKernel : public VmafSyclKernelShape<PROBE_SG, PROBE_GRF>
{
  public:
    TermKernel(const float *in, uint64_t *out) : in_(in), out_(out)
    {
    }

    VMAF_SYCL_FUNCTOR_SG_SIZE(PROBE_SG) void operator()(sycl::id<1> i) const
    {
        terms_of(in_ + INPUTS * i[0], out_ + OUTPUTS * i[0]);
    }

  private:
    const float *in_;
    uint64_t *out_;
};

void run_kernel(sycl::queue &q, const float *in, size_t n, uint64_t *out)
{
    const DeviceBlock<float> d_in(q, INPUTS * n);
    const DeviceBlock<uint64_t> d_out(q, OUTPUTS * n);
    q.memcpy(d_in.get(), in, INPUTS * n * sizeof(float));
    q.wait_and_throw();
    q.parallel_for(sycl::range<1>(n), TermKernel(d_in.get(), d_out.get()));
    q.wait_and_throw();
    q.memcpy(out, d_out.get(), OUTPUTS * n * sizeof(uint64_t));
    q.wait_and_throw();
}

} // namespace

/* The six terms of n samples on the host. `in` holds 7 floats per sample
 * (num_m, num_s, denom_s, r1, m1, r2, m2), `out` takes 6 bit patterns. */
extern "C" void vmaf_test_sycl_ss2_host(const float *in, size_t n, uint64_t *out)
{
    for (size_t i = 0; i < n; i++) {
        terms_of(in + INPUTS * i, out + OUTPUTS * i);
    }
}

/* The same in a kernel on the default GPU. 0, -EINVAL, -ENODEV without a
 * device, -EIO on a SYCL error. */
extern "C" int vmaf_test_sycl_ss2_device(const float *in, size_t n, uint64_t *out)
{
    if (in == nullptr || out == nullptr) {
        return -EINVAL;
    }
    if (n == 0) {
        return 0;
    }
    std::optional<sycl::device> device;
    try {
        device.emplace(sycl::gpu_selector_v);
    } catch (const sycl::exception &) {
        return -ENODEV;
    }
    try {
        sycl::queue q(*device);
        run_kernel(q, in, n, out);
    } catch (const std::exception &) {
        return -EIO;
    }
    return 0;
}
