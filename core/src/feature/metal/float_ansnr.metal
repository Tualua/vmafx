/**
 *  Copyright 2026 Lusoris and Claude (Anthropic)
 *  SPDX-License-Identifier: BSD-3-Clause-Plus-Patent
 *
 *  Metal compute kernel for float_ansnr (T8-1f / ADR-0421).
 *  Mirrors `libvmaf/src/feature/vulkan/shaders/float_ansnr.comp`.
 *
 *  Algorithm (must match CPU libvmaf/src/feature/ansnr.c::compute_ansnr,
 *  2D filter path, ANSNR_OPT_FILTER_1D off):
 *    1. Convert: val = (raw / scaler) - 128.0
 *       scaler: 1 for bpc=8, 4 for bpc=10, 16 for bpc=12, 256 for bpc=16.
 *    2. 3×3 ref filter (ansnr_filter2d_ref_s): {1,2,1; 2,4,2; 1,2,1} / 16
 *       → ref_filtr.
 *    3. 5×5 dis filter (ansnr_filter2d_dis_s):
 *       {2,7,12,7,2; 7,31,52,31,7; 12,52,127,52,12; ...} / 571 → filtd.
 *    4. sig   += ref_filtr * ref_filtr
 *       noise += (ref_filtr - filtd) * (ref_filtr - filtd)
 *    5. Host: ansnr = 10 * log10(sig_sum / noise_sum).
 *
 *  Mirror padding: edge-replicating (`2*sup - idx - 1` for idx >= sup),
 *  matching motion_v2 and NOT the skip-boundary form used by motion.
 *
 *  Reduction: per-WG (sig, noise) float partial pair using the standard
 *  simd_sum → threadgroup → partials[idx] pattern. No atomics.
 *
 *  Buffer bindings:
 *   [[buffer(0)]] ref         — const uchar *
 *   [[buffer(1)]] dis         — const uchar *
 *   [[buffer(2)]] sig_parts   — float * (grid_w × grid_h)
 *   [[buffer(3)]] noise_parts — float * (grid_w × grid_h)
 *   [[buffer(4)]] strides     — uint4 (.x=ref_stride, .y=dis_stride,
 *                                       .z=bpc, .w=unused)
 *   [[buffer(5)]] dim         — uint2 (width, height)
 */

#include <metal_stdlib>
using namespace metal;

/* 3×3 ref filter weights (ansnr_filter2d_ref_s / 16). */
constant float FILT_REF[9] = {
    1.0f/16.0f, 2.0f/16.0f, 1.0f/16.0f,
    2.0f/16.0f, 4.0f/16.0f, 2.0f/16.0f,
    1.0f/16.0f, 2.0f/16.0f, 1.0f/16.0f
};

/* 5×5 dis filter weights (ansnr_filter2d_dis_s / 571). */
constant float FILT_DIS[25] = {
     2.0f/571.0f,  7.0f/571.0f, 12.0f/571.0f,  7.0f/571.0f,  2.0f/571.0f,
     7.0f/571.0f, 31.0f/571.0f, 52.0f/571.0f, 31.0f/571.0f,  7.0f/571.0f,
    12.0f/571.0f, 52.0f/571.0f,127.0f/571.0f, 52.0f/571.0f, 12.0f/571.0f,
     7.0f/571.0f, 31.0f/571.0f, 52.0f/571.0f, 31.0f/571.0f,  7.0f/571.0f,
     2.0f/571.0f,  7.0f/571.0f, 12.0f/571.0f,  7.0f/571.0f,  2.0f/571.0f
};

#define HALF_FW 2
#define TILE_W  20   /* 16 + 2*2 */
#define TILE_H  20
/* Tile pitch padded to 21 to avoid bank conflicts on 32-bank Apple GPU. */
#define TILE_PITCH 21

static inline int dev_mirror(int idx, int sup) {
    if (idx < 0) { return -idx; }
    if (idx >= sup) { return 2 * sup - idx - 1; }
    return idx;
}

/* ------------------------------------------------------------------ */
/*  Inner kernel body (shared between 8bpc and 16bpc variants)         */
/* ------------------------------------------------------------------ */
static inline void ansnr_body(
    threadgroup float s_ref[TILE_H * TILE_PITCH],
    threadgroup float s_dis[TILE_H * TILE_PITCH],
    threadgroup float sg_sig[8],
    threadgroup float sg_noise[8],
    device float *sig_parts,
    device float *noise_parts,
    uint2 gid, uint2 bid, uint2 grid_groups,
    uint2 lid2, uint lid, uint simd_lane, uint simd_id, uint simd_count,
    int width, int height)
{
    /* Apply 3×3 ref filter (inner pixels only; halo already loaded). */
    float ref_filtr = 0.0f;
    {
        const int ty = (int)lid2.y + HALF_FW;
        const int tx = (int)lid2.x + HALF_FW;
        for (int ky = -1; ky <= 1; ++ky) {
            for (int kx = -1; kx <= 1; ++kx) {
                const int wi = (ky + 1) * 3 + (kx + 1);
                ref_filtr += FILT_REF[wi] *
                    s_ref[(ty + ky) * TILE_PITCH + (tx + kx)];
            }
        }
    }

    /* Apply 5×5 dis filter. */
    float filtd = 0.0f;
    {
        const int ty = (int)lid2.y + HALF_FW;
        const int tx = (int)lid2.x + HALF_FW;
        for (int ky = -2; ky <= 2; ++ky) {
            for (int kx = -2; kx <= 2; ++kx) {
                const int wi = (ky + 2) * 5 + (kx + 2);
                filtd += FILT_DIS[wi] *
                    s_dis[(ty + ky) * TILE_PITCH + (tx + kx)];
            }
        }
    }

    float my_sig = 0.0f, my_noise = 0.0f;
    if ((int)gid.x < width && (int)gid.y < height) {
        const float diff = ref_filtr - filtd;
        my_sig   = ref_filtr * ref_filtr;
        my_noise = diff * diff;
    }

    const float ls = simd_sum(my_sig);
    const float ln = simd_sum(my_noise);
    if (simd_lane == 0) {
        sg_sig[simd_id]   = ls;
        sg_noise[simd_id] = ln;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (lid == 0) {
        float gs = 0.0f, gn = 0.0f;
        for (uint i = 0; i < simd_count; ++i) {
            gs += sg_sig[i];
            gn += sg_noise[i];
        }
        const uint idx = bid.y * grid_groups.x + bid.x;
        sig_parts[idx]   = gs;
        noise_parts[idx] = gn;
    }
}

/* ------------------------------------------------------------------ */
/*  8 bpc kernel                                                        */
/* ------------------------------------------------------------------ */
kernel void float_ansnr_kernel_8bpc(
    const device uchar  *ref         [[buffer(0)]],
    const device uchar  *dis         [[buffer(1)]],
    device       float  *sig_parts   [[buffer(2)]],
    device       float  *noise_parts [[buffer(3)]],
    constant     uint2  &strides     [[buffer(4)]],
    constant     uint2  &dim         [[buffer(5)]],
    uint2  gid         [[thread_position_in_grid]],
    uint2  bid         [[threadgroup_position_in_grid]],
    uint2  grid_groups [[threadgroups_per_grid]],
    uint2  lid2        [[thread_position_in_threadgroup]],
    uint   lid         [[thread_index_in_threadgroup]],
    uint   simd_lane   [[thread_index_in_simdgroup]],
    uint   simd_id     [[simdgroup_index_in_threadgroup]],
    uint   simd_count  [[simdgroups_per_threadgroup]])
{
    const int width  = (int)dim.x;
    const int height = (int)dim.y;

    threadgroup float s_ref[TILE_H * TILE_PITCH];
    threadgroup float s_dis[TILE_H * TILE_PITCH];
    threadgroup float sg_sig[8];
    threadgroup float sg_noise[8];

    /* Load TILE_H × TILE_W tiles into shared memory (cooperative load). */
    const int wg_ox = (int)bid.x * 16 - HALF_FW;
    const int wg_oy = (int)bid.y * 16 - HALF_FW;
    const int n_tiles = TILE_W * TILE_H;
    const int wg_size = 16 * 16;
    for (int i = (int)lid; i < n_tiles; i += wg_size) {
        const int ty = i / TILE_W;
        const int tx = i % TILE_W;
        const int sy = dev_mirror(wg_oy + ty, height);
        const int sx = dev_mirror(wg_ox + tx, width);
        s_ref[ty * TILE_PITCH + tx] = (float)ref[sy * (int)strides.x + sx] - 128.0f;
        s_dis[ty * TILE_PITCH + tx] = (float)dis[sy * (int)strides.y + sx] - 128.0f;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

    ansnr_body(s_ref, s_dis, sg_sig, sg_noise,
               sig_parts, noise_parts,
               gid, bid, grid_groups,
               lid2, lid, simd_lane, simd_id, simd_count,
               width, height);
}

/* ------------------------------------------------------------------ */
/*  16 bpc kernel                                                       */
/* ------------------------------------------------------------------ */
kernel void float_ansnr_kernel_16bpc(
    const device uchar  *ref         [[buffer(0)]],
    const device uchar  *dis         [[buffer(1)]],
    device       float  *sig_parts   [[buffer(2)]],
    device       float  *noise_parts [[buffer(3)]],
    constant     uint4  &strides     [[buffer(4)]],
    constant     uint2  &dim         [[buffer(5)]],
    uint2  gid         [[thread_position_in_grid]],
    uint2  bid         [[threadgroup_position_in_grid]],
    uint2  grid_groups [[threadgroups_per_grid]],
    uint2  lid2        [[thread_position_in_threadgroup]],
    uint   lid         [[thread_index_in_threadgroup]],
    uint   simd_lane   [[thread_index_in_simdgroup]],
    uint   simd_id     [[simdgroup_index_in_threadgroup]],
    uint   simd_count  [[simdgroups_per_threadgroup]])
{
    const int width   = (int)dim.x;
    const int height  = (int)dim.y;
    const float scaler = (float)(1u << ((uint)strides.z - 8u));

    threadgroup float s_ref[TILE_H * TILE_PITCH];
    threadgroup float s_dis[TILE_H * TILE_PITCH];
    threadgroup float sg_sig[8];
    threadgroup float sg_noise[8];

    const int wg_ox = (int)bid.x * 16 - HALF_FW;
    const int wg_oy = (int)bid.y * 16 - HALF_FW;
    const int n_tiles = TILE_W * TILE_H;
    const int wg_size = 16 * 16;
    for (int i = (int)lid; i < n_tiles; i += wg_size) {
        const int ty = i / TILE_W;
        const int tx = i % TILE_W;
        const int sy = dev_mirror(wg_oy + ty, height);
        const int sx = dev_mirror(wg_ox + tx, width);
        const device ushort *rr =
            (const device ushort *)(ref + sy * (int)strides.x);
        const device ushort *dr =
            (const device ushort *)(dis + sy * (int)strides.y);
        s_ref[ty * TILE_PITCH + tx] = (float)rr[sx] / scaler - 128.0f;
        s_dis[ty * TILE_PITCH + tx] = (float)dr[sx] / scaler - 128.0f;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

    ansnr_body(s_ref, s_dis, sg_sig, sg_noise,
               sig_parts, noise_parts,
               gid, bid, grid_groups,
               lid2, lid, simd_lane, simd_id, simd_count,
               width, height);
}
