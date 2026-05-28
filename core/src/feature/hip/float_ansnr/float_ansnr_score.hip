/**
 *  Copyright 2026 Lusoris and Claude (Anthropic)
 *  SPDX-License-Identifier: BSD-3-Clause-Plus-Patent
 *
 *  HIP compute kernels for the float_ansnr feature extractor
 *  (T7-10b batch-1 / ADR-0372). Direct port of the CUDA twin at
 *  `libvmaf/src/feature/cuda/float_ansnr/float_ansnr_score.cu`.
 *
 *  Algorithm (must match CPU ansnr.c::compute_ansnr):
 *    1. Convert ref / dis pixels to float in [-128, 127.something]
 *       (`(raw / scaler) - 128.0` for the appropriate bpc scaler).
 *    2. Apply 3x3 ref filter (1/16 weights) -> ref_filtr.
 *    3. Apply 5x5 dis filter (weights /571) -> filtd.
 *    4. sig   += ref_filtr * ref_filtr
 *       noise += (ref_filtr - filtd)^2
 *
 *  Per-block partial pair layout:
 *      partials[2*block_idx + 0] = block sig sum (float)
 *      partials[2*block_idx + 1] = block noise sum (float)
 *  Host accumulates in `double` and emits float_ansnr + float_anpsnr.
 *
 *  HIP adaptation from CUDA:
 *  - Warp size is 64 on GCN/RDNA. `__shfl_down` used without mask
 *    (HIP convention; CUDA uses `__shfl_down_sync(0xffffffff, ...)`).
 *  - Shared memory warp-partial arrays sized for 64-lane warps:
 *    `ANSNR_BX * ANSNR_BY / ANSNR_WARP_SIZE` slots (256/64 = 4).
 *  - `VmafCudaBuffer partials` replaced by plain `float *partials`
 *    since the HIP host passes `rb.device` as a raw pointer.
 *  - Tile-load and filter loops are otherwise identical to the CUDA twin.
 *
 *  Mirror padding: edge-replicating reflective mirror
 *  (`2 * size - idx - 1`) — same as float_ansnr_cuda, NOT the
 *  skip-boundary variant the integer_motion kernel uses.
 */

#include <hip/hip_runtime.h>
#include <stdint.h>

#define ANSNR_BX 16
#define ANSNR_BY 16
#define ANSNR_HALF 2
#define ANSNR_TILE_W (ANSNR_BX + 2 * ANSNR_HALF)
#define ANSNR_TILE_H (ANSNR_BY + 2 * ANSNR_HALF)

/* HIP warp size is 64 on GCN/RDNA. */
#define ANSNR_WARP_SIZE 64
#define ANSNR_WARPS_PER_BLOCK (ANSNR_BX * ANSNR_BY / ANSNR_WARP_SIZE)

__device__ static const float ANSNR_FILT_REF[9] = {
    1.0f / 16.0f, 2.0f / 16.0f, 1.0f / 16.0f,
    2.0f / 16.0f, 4.0f / 16.0f, 2.0f / 16.0f,
    1.0f / 16.0f, 2.0f / 16.0f, 1.0f / 16.0f,
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

/* HIP warp-level reduction. */
__device__ __forceinline__ float ansnr_warp_reduce(float v)
{
    for (int off = ANSNR_WARP_SIZE / 2; off > 0; off >>= 1)
        v += __shfl_down(v, off);
    return v;
}

extern "C" {

/**
 * 8-bpc kernel: ref/dis are uint8_t row-major arrays with stride bytes
 * per row. Computes per-pixel (sig, noise) and accumulates one
 * interleaved float pair per block into partials[2*block_idx+0/+1].
 */
__global__ void float_ansnr_kernel_8bpc(const uint8_t *__restrict__ ref,
                                        const uint8_t *__restrict__ dis,
                                        ptrdiff_t ref_stride,
                                        ptrdiff_t dis_stride,
                                        float *__restrict__ partials,
                                        unsigned width,
                                        unsigned height)
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

    __shared__ float s_sig_warps[ANSNR_WARPS_PER_BLOCK];
    __shared__ float s_noise_warps[ANSNR_WARPS_PER_BLOCK];
    const float wsig = ansnr_warp_reduce(my_sig);
    const float wnoise = ansnr_warp_reduce(my_noise);
    const unsigned lane = lid % ANSNR_WARP_SIZE;
    const unsigned warp_id = lid / ANSNR_WARP_SIZE;
    if (lane == 0) {
        s_sig_warps[warp_id] = wsig;
        s_noise_warps[warp_id] = wnoise;
    }
    __syncthreads();
    if (lid == 0) {
        float bsig = 0.0f;
        float bnoise = 0.0f;
#pragma unroll
        for (int i = 0; i < ANSNR_WARPS_PER_BLOCK; i++) {
            bsig += s_sig_warps[i];
            bnoise += s_noise_warps[i];
        }
        const unsigned block_idx = blockIdx.y * gridDim.x + blockIdx.x;
        partials[2 * block_idx + 0] = bsig;
        partials[2 * block_idx + 1] = bnoise;
    }
}

/**
 * 10/12/16-bpc kernel: ref/dis are byte-addressed uint16 arrays.
 * Raw uint16 values are scaled by inv_scaler and shifted by -128
 * to match the CPU reference's picture_copy normalisation.
 */
__global__ void float_ansnr_kernel_16bpc(const uint8_t *__restrict__ ref,
                                         const uint8_t *__restrict__ dis,
                                         ptrdiff_t ref_stride,
                                         ptrdiff_t dis_stride,
                                         float *__restrict__ partials,
                                         unsigned width,
                                         unsigned height,
                                         unsigned bpc)
{
    __shared__ float s_ref[ANSNR_TILE_H][ANSNR_TILE_W];
    __shared__ float s_dis[ANSNR_TILE_H][ANSNR_TILE_W];

    float scaler = 1.0f;
    if (bpc == 10u)
        scaler = 4.0f;
    else if (bpc == 12u)
        scaler = 16.0f;
    else if (bpc == 16u)
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

    __shared__ float s_sig_warps[ANSNR_WARPS_PER_BLOCK];
    __shared__ float s_noise_warps[ANSNR_WARPS_PER_BLOCK];
    const float wsig = ansnr_warp_reduce(my_sig);
    const float wnoise = ansnr_warp_reduce(my_noise);
    const unsigned lane = lid % ANSNR_WARP_SIZE;
    const unsigned warp_id = lid / ANSNR_WARP_SIZE;
    if (lane == 0) {
        s_sig_warps[warp_id] = wsig;
        s_noise_warps[warp_id] = wnoise;
    }
    __syncthreads();
    if (lid == 0) {
        float bsig = 0.0f;
        float bnoise = 0.0f;
#pragma unroll
        for (int i = 0; i < ANSNR_WARPS_PER_BLOCK; i++) {
            bsig += s_sig_warps[i];
            bnoise += s_noise_warps[i];
        }
        const unsigned block_idx = blockIdx.y * gridDim.x + blockIdx.x;
        partials[2 * block_idx + 0] = bsig;
        partials[2 * block_idx + 1] = bnoise;
    }
}

} /* extern "C" */
