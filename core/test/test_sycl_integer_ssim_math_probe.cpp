/**
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: EUPL-1.2
 *
 *  SYCL side of test_sycl_integer_ssim_math (ADR-1443): the fp64 operations
 *  of feature/sycl/sycl_soft_signed.h and the per-pixel term of
 *  feature/sycl/sycl_integer_ssim_math.h, callable from the C test on the
 *  host and on the device. core/test/meson.build compiles this TU with the
 *  SYCL feature line and links it through sycl_dependency, so the kernels
 *  below are built exactly like the extractor's. Integers and fp32 only on
 *  the device (ADR-0220).
 */

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <new>
#include <optional>

#include <sycl/sycl.hpp>

#include "../src/feature/sycl/sycl_compat.h"
#include "../src/feature/sycl/sycl_integer_ssim_math.h"

namespace
{

using vmaf_sycl_issim::Moments;
using vmaf_sycl_issim::Stabilisers;
using vmaf_sycl_soft::SoftSigned;

/* The operations the C test names by number; any other number converts `a`
 * from an integer. */
constexpr int OP_ADD = 0;
constexpr int OP_SUB = 1;
constexpr int OP_MUL = 2;
constexpr int OP_DIV = 3;

/* The sub-group size and register file of the extractor's term kernel. */
constexpr int PROBE_SG = 16;
constexpr int PROBE_GRF = 256;

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

/* One operation on two fp64 bit patterns; OP_FROM_U64 reads `a` as an
 * integer. */
__attribute__((flatten, always_inline)) inline uint64_t operate(uint64_t a, uint64_t b, int op)
{
    const SoftSigned x = vmaf_sycl_soft::signed_from_bits(a);
    const SoftSigned y = vmaf_sycl_soft::signed_from_bits(b);
    uint64_t result = vmaf_sycl_soft::signed_bits(vmaf_sycl_soft::signed_from_u64(a));
    if (op == OP_ADD) {
        result = vmaf_sycl_soft::signed_bits(vmaf_sycl_soft::signed_add(x, y));
    }
    if (op == OP_SUB) {
        result = vmaf_sycl_soft::signed_bits(vmaf_sycl_soft::signed_sub(x, y));
    }
    if (op == OP_MUL) {
        result = vmaf_sycl_soft::signed_bits(vmaf_sycl_soft::signed_mul(x, y));
    }
    if (op == OP_DIV) {
        result = vmaf_sycl_soft::signed_bits(vmaf_sycl_soft::signed_div(x, y));
    }
    return result;
}

__attribute__((flatten, always_inline)) inline uint64_t term(const uint64_t *m,
                                                             const Stabilisers &k)
{
    return vmaf_sycl_issim::term_bits(
        Moments{.mux = m[0], .muy = m[1], .x2 = m[2], .xy = m[3], .y2 = m[4], .w = m[5]}, k);
}

struct OperationArgs {
    const uint64_t *a;
    const uint64_t *b;
    uint64_t *out;
    int op;
};

class OperationKernel : public VmafSyclKernelShape<PROBE_SG, PROBE_GRF>
{
  public:
    explicit OperationKernel(const OperationArgs &args) : a_(args)
    {
    }

    VMAF_SYCL_FUNCTOR_SG_SIZE(PROBE_SG) void operator()(sycl::id<1> i) const
    {
        a_.out[i] = operate(a_.a[i], a_.b[i], a_.op);
    }

  private:
    OperationArgs a_;
};

struct TermArgs {
    const uint64_t *moments;
    uint64_t *out;
    Stabilisers stabilisers;
};

class TermKernel : public VmafSyclKernelShape<PROBE_SG, PROBE_GRF>
{
  public:
    explicit TermKernel(const TermArgs &args) : a_(args)
    {
    }

    VMAF_SYCL_FUNCTOR_SG_SIZE(PROBE_SG) void operator()(sycl::id<1> i) const
    {
        a_.out[i] = term(a_.moments + 6u * i[0], a_.stabilisers);
    }

  private:
    TermArgs a_;
};

void run_operations(sycl::queue &q, const uint64_t *a, const uint64_t *b, size_t n, int op,
                    uint64_t *out)
{
    const DeviceBlock<uint64_t> block(q, 3 * n);
    uint64_t *d_a = block.get();
    uint64_t *d_b = d_a + n;
    uint64_t *d_out = d_b + n;
    q.memcpy(d_a, a, n * sizeof(uint64_t));
    q.memcpy(d_b, b, n * sizeof(uint64_t));
    q.wait_and_throw();
    q.parallel_for(sycl::range<1>(n),
                   OperationKernel(OperationArgs{.a = d_a, .b = d_b, .out = d_out, .op = op}));
    q.wait_and_throw();
    q.memcpy(out, d_out, n * sizeof(uint64_t));
    q.wait_and_throw();
}

void run_terms(sycl::queue &q, const uint64_t *moments, size_t n, unsigned bpc, uint64_t *out)
{
    const DeviceBlock<uint64_t> block(q, 7 * n);
    uint64_t *d_moments = block.get();
    uint64_t *d_out = d_moments + 6 * n;
    q.memcpy(d_moments, moments, 6 * n * sizeof(uint64_t));
    q.wait_and_throw();
    q.parallel_for(sycl::range<1>(n),
                   TermKernel(TermArgs{.moments = d_moments,
                                       .out = d_out,
                                       .stabilisers = vmaf_sycl_issim::make_stabilisers(bpc)}));
    q.wait_and_throw();
    q.memcpy(out, d_out, n * sizeof(uint64_t));
    q.wait_and_throw();
}

/* Runs `work` on the default GPU. 0, -ENODEV without a device, -EIO on a
 * SYCL error. */
template <typename Work> int on_device(Work work)
{
    std::optional<sycl::device> device;
    try {
        device.emplace(sycl::gpu_selector_v);
    } catch (const sycl::exception &) {
        return -ENODEV;
    }
    try {
        sycl::queue q(*device);
        work(q);
    } catch (const std::exception &) {
        return -EIO;
    }
    return 0;
}

} // namespace

/* out[i] = the fp64 bit pattern of a[i] op b[i], on the host. */
extern "C" void vmaf_test_sycl_soft_host(const uint64_t *a, const uint64_t *b, size_t n, int op,
                                         uint64_t *out)
{
    for (size_t i = 0; i < n; i++) {
        out[i] = operate(a[i], b[i], op);
    }
}

/* The same in a kernel on the default GPU. */
extern "C" int vmaf_test_sycl_soft_device(const uint64_t *a, const uint64_t *b, size_t n, int op,
                                          uint64_t *out)
{
    if (a == nullptr || b == nullptr || out == nullptr) {
        return -EINVAL;
    }
    if (n == 0) {
        return 0;
    }
    return on_device([=](sycl::queue &q) { run_operations(q, a, b, n, op, out); });
}

/* out[i] = the fp64 bit pattern of the term of the moments at 6 * i (mux,
 * muy, x2, xy, y2, w), on the host. */
extern "C" void vmaf_test_sycl_issim_host(const uint64_t *moments, size_t n, unsigned bpc,
                                          uint64_t *out)
{
    const Stabilisers k = vmaf_sycl_issim::make_stabilisers(bpc);
    for (size_t i = 0; i < n; i++) {
        out[i] = term(moments + 6u * i, k);
    }
}

/* The same in a kernel on the default GPU. */
extern "C" int vmaf_test_sycl_issim_device(const uint64_t *moments, size_t n, unsigned bpc,
                                           uint64_t *out)
{
    if (moments == nullptr || out == nullptr) {
        return -EINVAL;
    }
    if (n == 0) {
        return 0;
    }
    return on_device([=](sycl::queue &q) { run_terms(q, moments, n, bpc, out); });
}
