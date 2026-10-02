/**
 *  Copyright 2016-2026 Netflix, Inc.
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 *  CUDA compute kernel for the float_psnr feature extractor
 *  (T7-23 / batch 3 part 3b — ADR-0192 / ADR-0195). CUDA twin of
 *  float_psnr_vulkan.
 *
 *  Per-pixel `(ref - dis)^2` as float_psnr.c forms it, added per warp and
 *  per 16x16 block as an integer; the host adds the block sums and applies
 *  the CPU formula.
 *
 *  The sums are integers so that they are exact (ADR-1455). float_psnr.c
 *  adds `(double)(diff * diff)` per row and the rows in double: every term is
 *  a float, the running sum is exact, and its value is the exact sum of the
 *  terms. With `scaler` = 2^(bpc - 8), `diff` is the sample difference over
 *  `scaler`, and its float square is the float square of the sample
 *  difference over `scaler`^2. So the kernel squares the sample difference in
 *  float, as the CPU does, and adds that value as an integer:
 *    - up to 12 bits the square is below 2^24 and exact in float;
 *    - at 16 bits the square is rounded to 24 bits by the float multiply, as
 *      on the CPU, and is below 2^32.
 *  A block's 256 terms are below 2^40, so one uint64 per block holds their
 *  sum. The host adds the blocks in uint64 and divides by `scaler`^2, a power
 *  of two. Both sides then hold the exact sum of the same terms. A fp32 block
 *  sum, which this kernel had, is exact only at 8 bits: at 10, 12 and 16 bits
 *  it rounded once the block's rms difference reached 256 code values, which
 *  put the score up to 1.2e-7 dB from the CPU's.
 */

#include "cuda_helper.cuh"
#include "common.h"

#define FPSNR_BX 16
#define FPSNR_BY 16
#define FPSNR_WARPS (FPSNR_BX * FPSNR_BY / 32)

namespace
{

/* The float square of a sample difference: float_psnr.c's `diff * diff`
 * times `scaler`^2. Both factors are exact (the difference is below 2^16 in
 * magnitude) and the product is an integer below 2^32, rounded to 24 bits
 * above 2^24 as the CPU's is. */
__device__ __forceinline__ unsigned long long fpsnr_square(int ref, int dis)
{
    const float diff = (float)(ref - dis);
    return (unsigned long long)__fmul_rn(diff, diff);
}

/* The block's sum of `mine` over all threads, valid in thread 0. Threads
 * outside the frame pass 0 and still take part: every thread of the block
 * reaches the barrier, so the full masks are exact. The sum is integer, so
 * its order cannot change it. */
__device__ __forceinline__ unsigned long long fpsnr_block_sum(unsigned long long mine)
{
    __shared__ unsigned long long s_warps[FPSNR_WARPS];
    const unsigned lid = threadIdx.y * blockDim.x + threadIdx.x;
    unsigned long long v = mine;
    for (int off = 16; off > 0; off >>= 1)
        v += __shfl_down_sync(0xffffffffu, v, off);
    if ((lid & 31u) == 0u)
        s_warps[lid >> 5] = v;
    __syncthreads();

    unsigned long long total = 0ull;
    if (lid == 0u) {
#pragma unroll
        for (unsigned i = 0; i < FPSNR_WARPS; i++)
            total += s_warps[i];
    }
    return total;
}

/* One block of T samples: the squared differences of its pixels, added, at
 * partials[block]. */
template <typename T>
__device__ __forceinline__ void
fpsnr_block(const uint8_t *__restrict__ ref, const uint8_t *__restrict__ dis, ptrdiff_t ref_stride,
            ptrdiff_t dis_stride, VmafCudaBuffer partials, unsigned width, unsigned height)
{
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;

    unsigned long long my_noise = 0ull;
    if (x < (int)width && y < (int)height) {
        const T rv = reinterpret_cast<const T *>(ref + y * ref_stride)[x];
        const T dv = reinterpret_cast<const T *>(dis + y * dis_stride)[x];
        my_noise = fpsnr_square(rv, dv);
    }

    const unsigned long long total = fpsnr_block_sum(my_noise);
    if (threadIdx.x == 0u && threadIdx.y == 0u) {
        const unsigned block_idx = blockIdx.y * gridDim.x + blockIdx.x;
        reinterpret_cast<unsigned long long *>(partials.data)[block_idx] = total;
    }
}

} // namespace

extern "C" {

__global__ void float_psnr_kernel_8bpc(const uint8_t *__restrict__ ref,
                                       const uint8_t *__restrict__ dis, ptrdiff_t ref_stride,
                                       ptrdiff_t dis_stride, VmafCudaBuffer partials,
                                       unsigned width, unsigned height)
{
    fpsnr_block<uint8_t>(ref, dis, ref_stride, dis_stride, partials, width, height);
}

/* 10/12/16-bpc kernel: the samples are native uint16_t. The squared
 * differences are added in units of 1 / scaler^2 (scaler = 2^(bpc - 8)); the
 * host divides. */
__global__ void float_psnr_kernel_16bpc(const uint8_t *__restrict__ ref,
                                        const uint8_t *__restrict__ dis, ptrdiff_t ref_stride,
                                        ptrdiff_t dis_stride, VmafCudaBuffer partials,
                                        unsigned width, unsigned height)
{
    fpsnr_block<uint16_t>(ref, dis, ref_stride, dis_stride, partials, width, height);
}

} /* extern "C" */
