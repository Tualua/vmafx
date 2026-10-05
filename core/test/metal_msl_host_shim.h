/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * Metal Shading Language on the host: what a .metal kernel file needs from
 * <metal_stdlib> to compile unmodified as C++ on Linux and macOS, so that a
 * host test can run its kernel bodies on the CPU and compare the results with
 * the CPU extractor (ADR-1806). core/test/metal_msl_host/metal_stdlib
 * includes this header, and a test puts that directory first on its include
 * path; the kernel's `#include <metal_stdlib>` then lands here.
 *
 * What it maps:
 *   - address spaces: `device`, `thread` and `threadgroup` vanish and
 *     `constant` becomes `const`, so a kernel parameter is a plain pointer or
 *     reference and a program-scope constant is a C++ constant;
 *   - `kernel` vanishes: a kernel is an ordinary function the test calls once
 *     per thread position, with its grid, threadgroup and lane indices as
 *     arguments; the [[...]] attributes of its parameters are ignored;
 *   - the scalar and vector types the kernels use (uchar, ushort, uint, ulong,
 *     int4, uint2, uint3);
 *   - threadgroup atomics as plain loads and stores, and threadgroup_barrier()
 *     as a no-op.
 *
 * The last point is the shim's limit: a test runs each threadgroup as ONE
 * thread (lid 0, threads per threadgroup 1), so a kernel's loops over its
 * lanes cover every element and its integer reductions are the device's, but
 * a missing barrier, a race between lanes or a lane-count assumption is not
 * seen. Nor is anything the Metal compiler does differently from a C++
 * compiler (the kernels' integer arithmetic is C++14's; fp32 code is only as
 * close as -fno-fast-math -ffp-contract=off makes the two). The scalar types
 * need an LP64 host, where `long` is 64 bits as in Metal: Windows (LLP64)
 * does not build these tests.
 *
 * Include it only in a translation unit of its own that holds the .metal file
 * and its test entry points: the address-space macros would rewrite any
 * header included after it.
 */

#ifndef LIBVMAF_TEST_METAL_MSL_HOST_SHIM_H_
#define LIBVMAF_TEST_METAL_MSL_HOST_SHIM_H_

#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdlib>

static_assert(sizeof(long) == 8, "Metal's long is 64 bits: the shim needs an LP64 host");

/* [[buffer(n)]], [[thread_position_in_grid]] and the other Metal attributes
 * name nothing a C++ compiler knows. */
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunknown-attributes"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wattributes"
#endif

// NOLINTBEGIN(modernize-use-using): the Metal type names, spelled as Metal does (ADR-1806).
typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long ulong;
// NOLINTEND(modernize-use-using)

/* An aggregate: `int4(a, b, c, d)` in a kernel is C++20 parenthesized
 * aggregate initialization and `int4()` value-initializes it to zeros. */
struct int4 {
    int x;
    int y;
    int z;
    int w;
};

struct uint2 {
    uint x;
    uint y;
};

struct uint3 {
    uint x;
    uint y;
    uint z;
};

/* Threadgroup atomics of a one-thread threadgroup. */
struct atomic_uint {
    uint v;
};

enum metal_shim_memory_order : unsigned char { memory_order_relaxed };

static inline uint atomic_fetch_add_explicit(atomic_uint *a, uint v, metal_shim_memory_order)
{
    const uint old = a->v;
    a->v = old + v;
    return old;
}

static inline void atomic_store_explicit(atomic_uint *a, uint v, metal_shim_memory_order)
{
    a->v = v;
}

static inline uint atomic_load_explicit(const atomic_uint *a, metal_shim_memory_order)
{
    return a->v;
}

struct mem_flags {
    enum : unsigned char { mem_none = 0, mem_device = 1, mem_threadgroup = 2 };
};

static inline void threadgroup_barrier(int flags)
{
    (void)flags;
}

namespace metal
{
} // namespace metal

using std::abs;

template <typename T> static inline T max(T a, T b)
{
    return a > b ? a : b;
}

template <typename T> static inline T min(T a, T b)
{
    return a < b ? a : b;
}

// NOLINTBEGIN(cppcoreguidelines-macro-usage,bugprone-reserved-identifier): Metal's keywords (ADR-1806).
#define kernel
#define device
#define constant const
#define threadgroup
#define thread
// NOLINTEND(cppcoreguidelines-macro-usage,bugprone-reserved-identifier)

#endif /* LIBVMAF_TEST_METAL_MSL_HOST_SHIM_H_ */
