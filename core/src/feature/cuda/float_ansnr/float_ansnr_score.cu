/**
 *  Copyright 2026 Lusoris and Claude (Anthropic)
 *  SPDX-License-Identifier: BSD-3-Clause-Plus-Patent
 *
 *  CUDA compute kernel for the float_ansnr feature extractor
 *  (T7-23 / batch 3 part 2b — ADR-0192 / ADR-0194). CUDA twin of
 *  float_ansnr_vulkan (PR sibling). Direct port of the per-pixel
 *  filter+MSE in libvmaf/src/feature/ansnr.c::compute_ansnr.
 *
 *  Algorithm (must match CPU):
 *   1. Convert ref / dis pixels to float in [-128, 127.something]
 *      (`(raw / scaler) - 128.0` where scaler matches CPU
 *      picture_copy: 1 for bpc=8, 4/16/256 for bpc=10/12/16).
 *   2. Apply 3x3 ref filter (1/16 weights) -> ref_filtr.
 *   3. Apply 5x5 dis filter (sums to 1.0, weights /571) -> filtd.
 *   4. sig   += ref_filtr * ref_filtr
 *      noise += (ref_filtr - filtd) * (ref_filtr - filtd)
 *
 *  Per-block partial pair (sig, noise) layout:
 *     partials[2*block_idx + 0] = block sig sum (float)
 *     partials[2*block_idx + 1] = block noise sum (float)
 *  Host accumulates in `double` and emits the two CPU outputs.
 *
 *  Mirror padding: edge-replicating reflective mirror
 *  (`2 * size - idx - 1`) — this intentionally differs from
 *  motion_v2_cuda's CPU-parity reflect-101 mirror
 *  (`2 * size - idx - 2`).
 *
 *  Precision contract per ADR-0192: places=3 (float convolution +
 *  per-WG reduction + log10 final transform). Empirically lands at
 *  places=4+ on the cross-backend gate fixture.
 */

#include "cuda_helper.cuh"
#include "common.h"

#define ANSNR_BX 16
#define ANSNR_BY 16
#define ANSNR_HALF 2
#define ANSNR_TILE_W (ANSNR_BX + 2 * ANSNR_HALF)
#define ANSNR_TILE_H (ANSNR_BY + 2 * ANSNR_HALF)

__device__ static const float ANSNR_FILT_REF[9] = {
    1.0f / 16.0f, 2.0f / 16.0f, 1.0f / 16.0f, 2.0f / 16.0f, 4.0f / 16.0f,
    2.0f / 16.0f, 1.0f / 16.0f, 2.0f / 16.0f, 1.0f / 16.0f,
};

__device__ static const float ANSNR_FILT_DIS[25] = {
    2.0f / 571.0f,  7.0f / 571.0f,  12.0f / 571.0f,  7.0f / 571.0f,  2.0f / 571.0f,
    7.0f / 571.0f,  31.0f / 571.0f, 52.0f / 571.0f,  31.0f / 571.0f, 7.0f / 571.0f,
    12.0f / 571.0f, 52.0f / 571.0f, 127.0f / 571.0f, 52.0f / 571.0f, 12.0f / 571.0f,
    7.0f / 571.0f,  31.0f / 571.0f, 52.0f / 571.0f,  31.0f / 571.0f, 7.0f / 571.0f,
    2.0f / 571.0f,  7.0f / 571.0f,  12.0f / 571.0f,  7.0f / 571.0f,  2.0f / 571.0f,
};

__device__ __forceinline__ int ansnr_mirror(int idx, int sup)
{
    if (idx < 0)
        return -idx;
    if (idx >= sup)
        return 2 * sup - idx - 1;
    return idx;
}

__device__ __forceinline__ float ansnr_warp_reduce(float v)
{
    for (int off = 16; off > 0; off >>= 1)
        v += __shfl_down_sync(0xffffffff, v, off);
    return v;
}

extern "C" {

__global__ void float_ansnr_kernel_8bpc(const uint8_t *__restrict__ ref,
                                        const uint8_t *__restrict__ dis, ptrdiff_t ref_stride,
                                        ptrdiff_t dis_stride, VmafCudaBuffer partials,
                                        unsigned width, unsigned height)
{
    __shared__ float s_ref[ANSNR_TILE_H][ANSNR_TILE_W];
    __shared__ float s_dis[ANSNR_TILE_H][ANSNR_TILE_W];

    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    const unsigned lid = threadIdx.y * blockDim.x + threadIdx.x;

    const int tile_ox = blockIdx.x * ANSNR_BX - ANSNR_HALF;
    const int tile_oy = blockIdx.y * ANSNR_BY - ANSNR_HALF;
    const unsigned tile_elems = ANSNR_TILE_H * ANSNR_TILE_W;
    const unsigned wg_size = ANSNR_BX * ANSNR_BY;

    for (unsigned i = lid; i < tile_elems; i += wg_size) {
        const unsigned tr = i / ANSNR_TILE_W;
        const unsigned tc = i % ANSNR_TILE_W;
        const int gx = ansnr_mirror(tile_ox + (int)tc, (int)width);
        const int gy = ansnr_mirror(tile_oy + (int)tr, (int)height);
        s_ref[tr][tc] = (float)ref[gy * ref_stride + gx] - 128.0f;
        s_dis[tr][tc] = (float)dis[gy * dis_stride + gx] - 128.0f;
    }
    __syncthreads();

    float my_sig = 0.0f;
    float my_noise = 0.0f;
    if (x < (int)width && y < (int)height) {
        const unsigned lx = threadIdx.x;
        const unsigned ly = threadIdx.y;

        float ref_filtr = 0.0f;
#pragma unroll
        for (int k = 0; k < 3; k++) {
#pragma unroll
            for (int l = 0; l < 3; l++) {
                ref_filtr += ANSNR_FILT_REF[k * 3 + l] * s_ref[ly + 1 + k][lx + 1 + l];
            }
        }
        float filtd = 0.0f;
#pragma unroll
        for (int k = 0; k < 5; k++) {
#pragma unroll
            for (int l = 0; l < 5; l++) {
                filtd += ANSNR_FILT_DIS[k * 5 + l] * s_dis[ly + k][lx + l];
            }
        }
        my_sig = ref_filtr * ref_filtr;
        const float diff = ref_filtr - filtd;
        my_noise = diff * diff;
    }

    __shared__ float s_sig_warps[ANSNR_BX * ANSNR_BY / 32];
    __shared__ float s_noise_warps[ANSNR_BX * ANSNR_BY / 32];
    const float wsig = ansnr_warp_reduce(my_sig);
    const float wnoise = ansnr_warp_reduce(my_noise);
    const int lane = lid % 32;
    const int warp_id = lid / 32;
    if (lane == 0) {
        s_sig_warps[warp_id] = wsig;
        s_noise_warps[warp_id] = wnoise;
    }
    __syncthreads();
    if (lid == 0) {
        float bsig = 0.0f;
        float bnoise = 0.0f;
#pragma unroll
        for (int i = 0; i < ANSNR_BX * ANSNR_BY / 32; i++) {
            bsig += s_sig_warps[i];
            bnoise += s_noise_warps[i];
        }
        const unsigned block_idx = blockIdx.y * gridDim.x + blockIdx.x;
        reinterpret_cast<float *>(partials.data)[2 * block_idx + 0] = bsig;
        reinterpret_cast<float *>(partials.data)[2 * block_idx + 1] = bnoise;
    }
}

__global__ void float_ansnr_kernel_16bpc(const uint8_t *__restrict__ ref,
                                         const uint8_t *__restrict__ dis, ptrdiff_t ref_stride,
                                         ptrdiff_t dis_stride, VmafCudaBuffer partials,
                                         unsigned width, unsigned height, unsigned bpc)
{
    __shared__ float s_ref[ANSNR_TILE_H][ANSNR_TILE_W];
    __shared__ float s_dis[ANSNR_TILE_H][ANSNR_TILE_W];

    float scaler = 1.0f;
    if (bpc == 10)
        scaler = 4.0f;
    else if (bpc == 12)
        scaler = 16.0f;
    else if (bpc == 16)
        scaler = 256.0f;
    const float inv_scaler = 1.0f / scaler;

    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    const unsigned lid = threadIdx.y * blockDim.x + threadIdx.x;

    const int tile_ox = blockIdx.x * ANSNR_BX - ANSNR_HALF;
    const int tile_oy = blockIdx.y * ANSNR_BY - ANSNR_HALF;
    const unsigned tile_elems = ANSNR_TILE_H * ANSNR_TILE_W;
    const unsigned wg_size = ANSNR_BX * ANSNR_BY;

    for (unsigned i = lid; i < tile_elems; i += wg_size) {
        const unsigned tr = i / ANSNR_TILE_W;
        const unsigned tc = i % ANSNR_TILE_W;
        const int gx = ansnr_mirror(tile_ox + (int)tc, (int)width);
        const int gy = ansnr_mirror(tile_oy + (int)tr, (int)height);
        const uint16_t r = reinterpret_cast<const uint16_t *>(ref + gy * ref_stride)[gx];
        const uint16_t d = reinterpret_cast<const uint16_t *>(dis + gy * dis_stride)[gx];
        s_ref[tr][tc] = (float)r * inv_scaler - 128.0f;
        s_dis[tr][tc] = (float)d * inv_scaler - 128.0f;
    }
    __syncthreads();

    float my_sig = 0.0f;
    float my_noise = 0.0f;
    if (x < (int)width && y < (int)height) {
        const unsigned lx = threadIdx.x;
        const unsigned ly = threadIdx.y;

        float ref_filtr = 0.0f;
#pragma unroll
        for (int k = 0; k < 3; k++) {
#pragma unroll
            for (int l = 0; l < 3; l++) {
                ref_filtr += ANSNR_FILT_REF[k * 3 + l] * s_ref[ly + 1 + k][lx + 1 + l];
            }
        }
        float filtd = 0.0f;
#pragma unroll
        for (int k = 0; k < 5; k++) {
#pragma unroll
            for (int l = 0; l < 5; l++) {
                filtd += ANSNR_FILT_DIS[k * 5 + l] * s_dis[ly + k][lx + l];
            }
        }
        my_sig = ref_filtr * ref_filtr;
        const float diff = ref_filtr - filtd;
        my_noise = diff * diff;
    }

    __shared__ float s_sig_warps[ANSNR_BX * ANSNR_BY / 32];
    __shared__ float s_noise_warps[ANSNR_BX * ANSNR_BY / 32];
    const float wsig = ansnr_warp_reduce(my_sig);
    const float wnoise = ansnr_warp_reduce(my_noise);
    const int lane = lid % 32;
    const int warp_id = lid / 32;
    if (lane == 0) {
        s_sig_warps[warp_id] = wsig;
        s_noise_warps[warp_id] = wnoise;
    }
    __syncthreads();
    if (lid == 0) {
        float bsig = 0.0f;
        float bnoise = 0.0f;
#pragma unroll
        for (int i = 0; i < ANSNR_BX * ANSNR_BY / 32; i++) {
            bsig += s_sig_warps[i];
            bnoise += s_noise_warps[i];
        }
        const unsigned block_idx = blockIdx.y * gridDim.x + blockIdx.x;
        reinterpret_cast<float *>(partials.data)[2 * block_idx + 0] = bsig;
        reinterpret_cast<float *>(partials.data)[2 * block_idx + 1] = bnoise;
    }
}

} /* extern "C" */
