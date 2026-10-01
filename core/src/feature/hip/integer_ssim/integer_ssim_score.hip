/**
 *  Copyright 2016-2026 Netflix, Inc.
 *  Copyright (c) 2011, Tom Distler (http://tdistler.com)
 *  Copyright 2001-2012 Xiph.Org and contributors.
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: BSD-2-Clause-Patent AND BSD-3-Clause AND BSD-2-Clause
 *
 *  HIP compute kernels for the integer_ssim feature extractor (ADR-0564).
 *  Port of the CUDA twin `cuda/integer_ssim/integer_ssim_score.cu`, which in
 *  turn ports `calc_ssim()` from the CPU reference `integer_ssim.c`.
 *
 *  The CPU algorithm, and therefore this one:
 *    - a 9-tap Gaussian with INTEGER weights [2,9,28,55,68,55,28,9,2]
 *      (`gaussian_filter_init(1.5, 5)`, KERNEL_WEIGHT = 256);
 *    - int64 accumulation of the moments mux, muy, x2, xy, y2 and the
 *      window weight w;
 *    - boundary truncation: taps that fall outside the frame are skipped
 *      and w counts only the in-bounds taps (no clamping, no mirroring);
 *    - the SSIM term of each pixel in double, from the int64 moments;
 *    - ssim = sum(term) / sum(w) over every pixel of the frame.
 *
 *  Two passes, both over the full W x H frame:
 *    1. integer_ssim_horiz_{8,16}bpc — horizontal 9-tap moments of one row
 *       into six W x H int64 planes (mux, muy, x2, xy, y2, w).
 *    2. integer_ssim_vert_terms — the vertical 9-tap over those planes and
 *       the SSIM term of every pixel in double, stored unreduced at its
 *       raster position, plus one int64 weight sum per block.
 *  The host adds the terms in raster order and the block weights, and divides
 *  the term sum by the weight sum (ADR-1438).
 *
 *  Exactness against the CPU:
 *    - The moments are int64 sums of int64 products, so they are exact and
 *      identical to the CPU's.
 *    - `issim_cpu_term` evaluates the expression of
 *      `ssim_reduce_row_range()` operand for operand, and meson builds this
 *      file with `-ffp-contract=off` (`hip_strict_fp_args`), so no product
 *      is fused into an FMA and each per-pixel term rounds exactly as the
 *      CPU's does.
 *    - The terms are not added here. A sum of doubles is its order:
 *      calc_ssim() adds them left to right and top to bottom into one
 *      running double, and no reduction over blocks or waves rounds like
 *      that sum. The host adds the read-back plane in index order, which is
 *      the CPU's order, so the frame score is the CPU's double at every
 *      frame size (ADR-1400 did this up to 4096 pixels; a block tree above
 *      that left the score up to 1.1e-11 away). That also decides identical
 *      frames as the CPU does: its quotient on an identical window is the
 *      weight up to an ulp, and whether the running sum absorbs the ulp
 *      depends on the frame (an identical 1x1 frame of zeros scores
 *      1 - 2^-52, 156.54 dB with `enable_db`).
 *    - The weights are integers; their sum does not depend on the order and
 *      is reduced per block.
 *
 *  HIP adaptation from the CUDA twin: the block reduction of the weights is
 *  a shared-memory tree over all ISSIM_BLOCK_SZ threads instead of a 32-lane
 *  `__shfl_down_sync` reduction, so it is independent of the wavefront size
 *  (wave32 on RDNA, wave64 on GCN / CDNA).
 */

#include <hip/hip_runtime.h>

#include <cstddef>
#include <cstdint>

/* Launch geometry. integer_ssim_hip.c launches every kernel here with
 * ISSIM_HIP_BLOCK_X x ISSIM_HIP_BLOCK_Y threads; the two must agree, the
 * shared-memory reduction below is sized for exactly that block. */
#define ISSIM_BLOCK_X 16
#define ISSIM_BLOCK_Y 8
#define ISSIM_BLOCK_SZ (ISSIM_BLOCK_X * ISSIM_BLOCK_Y)

/* kernel_len = 4 and kernel_sz = 2 * kernel_len + 1 = 9 in calc_ssim(). */
#define ISSIM_HALF_K 4
#define ISSIM_K_SZ 9

/* SSIM_K1 / SSIM_K2 of integer_ssim.c, spelled the same way so the constant
 * folds to the same double. The literals 0.0001 / 0.0009 are different
 * doubles. */
#define ISSIM_K1 (0.01 * 0.01)
#define ISSIM_K2 (0.03 * 0.03)

/* gaussian_filter_init(1.5, 5) with KERNEL_WEIGHT 256: the outer taps are
 * round(256 * exp(-i^2 / 4.5) / (1.5 * sqrt(2 * pi))) and the centre tap is
 * 256 minus the rest, giving a sum of exactly 256. */
__device__ static const int32_t ISSIM_KERNEL[ISSIM_K_SZ] = {2, 9, 28, 55, 68, 55, 28, 9, 2};

namespace
{

/* The taps [k_min, k_max) of the window centred on `pos` that fall inside
 * [0, len): the k_min / k_max formulas of ssim_accumulate_row() and
 * calc_ssim(). */
__device__ __forceinline__ int issim_tap_min(int pos)
{
    return pos < ISSIM_HALF_K ? ISSIM_HALF_K - pos : 0;
}

__device__ __forceinline__ int issim_tap_max(int pos, int len)
{
    return pos + ISSIM_HALF_K >= len ? ISSIM_K_SZ - (pos + ISSIM_HALF_K - len + 1) : ISSIM_K_SZ;
}

/* Pass 1 body, shared by the 8-bit and the 16-bit entry point. Reads one
 * packed row of `Sample` values and writes the six horizontal moments of
 * pixel (x, y). */
template <typename Sample>
__device__ __forceinline__ void
issim_horiz(const uint8_t *__restrict__ ref, ptrdiff_t ref_stride, const uint8_t *__restrict__ cmp,
            ptrdiff_t cmp_stride, int64_t *__restrict__ d_mux, int64_t *__restrict__ d_muy,
            int64_t *__restrict__ d_x2, int64_t *__restrict__ d_xy, int64_t *__restrict__ d_y2,
            int64_t *__restrict__ d_w, unsigned width, unsigned height)
{
    const unsigned x = blockIdx.x * blockDim.x + threadIdx.x;
    const unsigned y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height)
        return;

    const Sample *ref_row = reinterpret_cast<const Sample *>(ref + (ptrdiff_t)y * ref_stride);
    const Sample *cmp_row = reinterpret_cast<const Sample *>(cmp + (ptrdiff_t)y * cmp_stride);
    const int k_min = issim_tap_min((int)x);
    const int k_max = issim_tap_max((int)x, (int)width);

    int64_t mux = 0;
    int64_t muy = 0;
    int64_t x2 = 0;
    int64_t xy = 0;
    int64_t y2 = 0;
    int64_t w = 0;
    for (int k = k_min; k < k_max; k++) {
        const int src_x = (int)x - ISSIM_HALF_K + k;
        const int64_t s = ref_row[src_x];
        const int64_t d = cmp_row[src_x];
        const int64_t wk = ISSIM_KERNEL[k];
        mux += wk * s;
        muy += wk * d;
        x2 += wk * s * s;
        xy += wk * s * d;
        y2 += wk * d * d;
        w += wk;
    }

    const size_t idx = (size_t)y * width + x;
    d_mux[idx] = mux;
    d_muy[idx] = muy;
    d_x2[idx] = x2;
    d_xy[idx] = xy;
    d_y2[idx] = y2;
    d_w[idx] = w;
}

/* The six moments of one window. */
struct IssimMoments {
    int64_t mux;
    int64_t muy;
    int64_t x2;
    int64_t xy;
    int64_t y2;
    int64_t w;
};

/* The four factors of one pixel's SSIM term and its window weight. */
struct IssimFactors {
    double w_d;
    double lum_num;
    double cs_num;
    double lum_den;
    double cs_den;
};

/* Vertical 9-tap of pixel (x, y) over the pass-1 planes: the moment loop of
 * ssim_reduce_row_range(). Taps outside the frame are skipped. */
__device__ __forceinline__ IssimMoments
issim_vert_moments(const int64_t *__restrict__ d_mux_h, const int64_t *__restrict__ d_muy_h,
                   const int64_t *__restrict__ d_x2_h, const int64_t *__restrict__ d_xy_h,
                   const int64_t *__restrict__ d_y2_h, const int64_t *__restrict__ d_w_h,
                   unsigned x, unsigned y, unsigned width, unsigned height)
{
    const int k_min = issim_tap_min((int)y);
    const int k_max = issim_tap_max((int)y, (int)height);
    IssimMoments m = {0, 0, 0, 0, 0, 0};
    for (int k = k_min; k < k_max; k++) {
        const size_t hidx = (size_t)((int)y - ISSIM_HALF_K + k) * width + x;
        const int64_t vk = ISSIM_KERNEL[k];
        m.mux += vk * d_mux_h[hidx];
        m.muy += vk * d_muy_h[hidx];
        m.x2 += vk * d_x2_h[hidx];
        m.xy += vk * d_xy_h[hidx];
        m.y2 += vk * d_y2_h[hidx];
        m.w += vk * d_w_h[hidx];
    }
    return m;
}

/* The factors of one pixel's SSIM term: the operands of the expression in
 * ssim_reduce_row_range() (integer_ssim.c), each formed in the CPU's order
 * (requires -ffp-contract=off). */
__device__ __forceinline__ IssimFactors issim_factors(const IssimMoments &m, double sm)
{
    IssimFactors f;
    f.w_d = (double)m.w;
    const double c1 = sm * sm * ISSIM_K1 * f.w_d * f.w_d;
    const double c2 = sm * sm * ISSIM_K2 * f.w_d * f.w_d;
    const double mx2 = (double)m.mux * (double)m.mux;
    const double mxy = (double)m.mux * (double)m.muy;
    const double my2 = (double)m.muy * (double)m.muy;
    f.lum_num = 2.0 * mxy + c1;
    f.cs_num = c2 + 2.0 * ((double)m.xy * f.w_d - mxy);
    f.lum_den = mx2 + my2 + c1;
    f.cs_den = (double)m.x2 * f.w_d - mx2 + (double)m.y2 * f.w_d - my2 + c2;
    return f;
}

/* One pixel's SSIM term, weighted by its window weight, exactly as the CPU
 * forms it: the product and the quotient in the CPU's order, which gives the
 * same double. The per-pixel form writes these. */
__device__ __forceinline__ double issim_cpu_term(const IssimFactors &f)
{
    return f.w_d * f.lum_num * f.cs_num / (f.lum_den * f.cs_den);
}

} /* namespace */

extern "C" {

/* Pass 1, 8-bit samples. */
__global__ void integer_ssim_horiz_8bpc(const uint8_t *__restrict__ ref, ptrdiff_t ref_stride,
                                        const uint8_t *__restrict__ cmp, ptrdiff_t cmp_stride,
                                        int64_t *__restrict__ d_mux, int64_t *__restrict__ d_muy,
                                        int64_t *__restrict__ d_x2, int64_t *__restrict__ d_xy,
                                        int64_t *__restrict__ d_y2, int64_t *__restrict__ d_w,
                                        unsigned width, unsigned height)
{
    issim_horiz<uint8_t>(ref, ref_stride, cmp, cmp_stride, d_mux, d_muy, d_x2, d_xy, d_y2, d_w,
                         width, height);
}

/* Pass 1, 9..16-bit samples stored as little-endian uint16_t. The raw sample
 * values are used, as on the CPU; the bit depth only enters pass 2 through
 * `samplemax`. */
__global__ void integer_ssim_horiz_16bpc(const uint8_t *__restrict__ ref, ptrdiff_t ref_stride,
                                         const uint8_t *__restrict__ cmp, ptrdiff_t cmp_stride,
                                         int64_t *__restrict__ d_mux, int64_t *__restrict__ d_muy,
                                         int64_t *__restrict__ d_x2, int64_t *__restrict__ d_xy,
                                         int64_t *__restrict__ d_y2, int64_t *__restrict__ d_w,
                                         unsigned width, unsigned height)
{
    issim_horiz<uint16_t>(ref, ref_stride, cmp, cmp_stride, d_mux, d_muy, d_x2, d_xy, d_y2, d_w,
                          width, height);
}

/* Pass 2: vertical 9-tap over the pass-1 planes and every pixel's CPU term,
 * unreduced, at terms[y * width + x]. The host adds the terms in index order,
 * which is the raster order of calc_ssim() / ssim_reduce_row_range(), so the
 * sum is the CPU's `ssim` (ADR-1438). The window weights are integers: one
 * sum per block at block_weights[blockIdx.y * gridDim.x + blockIdx.x], which
 * the host adds to the CPU's `ssimw`. `samplemax` is (1 << bpc) - 1. Launched
 * over the pass-1 grid. */
__global__ void
integer_ssim_vert_terms(const int64_t *__restrict__ d_mux_h, const int64_t *__restrict__ d_muy_h,
                        const int64_t *__restrict__ d_x2_h, const int64_t *__restrict__ d_xy_h,
                        const int64_t *__restrict__ d_y2_h, const int64_t *__restrict__ d_w_h,
                        double *__restrict__ terms, int64_t *__restrict__ block_weights,
                        unsigned width, unsigned height, double samplemax)
{
    const unsigned x = blockIdx.x * blockDim.x + threadIdx.x;
    const unsigned y = blockIdx.y * blockDim.y + threadIdx.y;

    /* Threads outside the frame contribute weight 0 and still take part in
     * the reduction: every thread of the block must reach the barriers. */
    int64_t my_weight = 0;
    if (x < width && y < height) {
        const IssimMoments m = issim_vert_moments(d_mux_h, d_muy_h, d_x2_h, d_xy_h, d_y2_h, d_w_h,
                                                  x, y, width, height);
        terms[(size_t)y * width + x] = issim_cpu_term(issim_factors(m, samplemax));
        my_weight = m.w;
    }

    __shared__ int64_t s_weight[ISSIM_BLOCK_SZ];
    const unsigned tid = threadIdx.y * blockDim.x + threadIdx.x;
    s_weight[tid] = my_weight;
    __syncthreads();
    for (unsigned half = ISSIM_BLOCK_SZ / 2u; half > 0u; half >>= 1u) {
        if (tid < half)
            s_weight[tid] += s_weight[tid + half];
        __syncthreads();
    }
    if (tid == 0u)
        block_weights[blockIdx.y * gridDim.x + blockIdx.x] = s_weight[0];
}

} /* extern "C" */
