/**
 *  Copyright 2001-2012 Xiph.Org and contributors.
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: BSD-2-Clause-Patent AND BSD-2-Clause
 *
 *  Metal compute kernel for the psnr_hvs feature extractor — Metal twin
 *  of the CUDA reference
 *  core/src/feature/cuda/integer_psnr_hvs/psnr_hvs_score.cu and the
 *  CPU reference core/src/feature/third_party/xiph/psnr_hvs.c
 *  (feature name "psnr_hvs", emits psnr_hvs_y / psnr_hvs_cb /
 *  psnr_hvs_cr / psnr_hvs).
 *
 *  ADR-1397 / ADR-1401 / ADR-1498: the scores are the CPU extractor's bit
 *  for bit. calc_psnrhvs() adds every masked coefficient error of a plane
 *  into one running float, so the kernel computes the 64 terms of every
 *  block in the CPU's arithmetic and stores them all, in the CPU's
 *  row-major order; the host adds them with vmaf_psnr_hvs_plane_score()
 *  (integer_psnr_hvs_metal.mm). No sum of terms on the device: a per-block
 *  float partial rounds differently from the CPU's running sum.
 *
 *  Every operation on a value comes from metal_psnr_hvs_math.h, which
 *  test_metal_psnr_hvs_math compiles on the host and holds against the CPU
 *  extractor: the variance ratio, the integer DCT, the masking energy, the
 *  masking threshold (a float product and its correctly rounded fp32 root,
 *  which is the CPU's double root rounded to float, ADR-1488) and the term
 *  (the integer coefficient difference, converted once). The masking table
 *  is the CPU's double product stored as float; MSL has no double, so the
 *  host forms it (vmaf_psnr_hvs_mask_value()) and binds it here. The
 *  kernels compile with -fno-fast-math -ffp-contract=off
 *  (metal_shader_strict_fp_args, core/src/metal/meson.build).
 *
 *  One threadgroup per 8x8 image block (sliding window, step 7), 64
 *  threads (8x8):
 *    - the 64 threads load the reference and distorted samples;
 *    - thread 0 takes both variance ratios in the CPU's i, j order;
 *    - the first eight threads run od_bin_fdct8x8() in place, columns
 *      first;
 *    - thread 0 takes both masking energies and the block's masking
 *      threshold;
 *    - every thread stores the term of its coefficient.
 *
 *  bpc handling: the 8bpc kernel reads raw uchar samples, the 16bpc kernel
 *  raw ushort samples, as the CPU feeds the raw 9- to 12-bit values into
 *  the DCT and divides the plane score by samplemax^2.
 *
 *  Buffer bindings (same for 8bpc and 16bpc; sample width differs):
 *   [[buffer(0)]] ref       — const uchar *  (plane, byte-addressed)
 *   [[buffer(1)]] dis       — const uchar *
 *   [[buffer(2)]] terms     — float *  (64 per block, blocks in raster order)
 *   [[buffer(3)]] csf       — const float * (64 CSF entries for the plane)
 *   [[buffer(4)]] dims      — uint4 (.x=width, .y=height,
 *                                    .z=num_blocks_x, .w=num_blocks_y)
 *   [[buffer(5)]] strides   — uint2 (.x=ref_row_bytes, .y=dis_row_bytes)
 *   [[buffer(6)]] mask      — const float * (64 masking-table entries)
 */

#include <metal_stdlib>
using namespace metal;

#include "metal_psnr_hvs_math.h"

/* ------------------------------------------------------------------ */
/*  Per-block helpers on metal_psnr_hvs_math.h                          */
/* ------------------------------------------------------------------ */

/* od_bin_fdct8x8() of the block in `blk`, in place: lanes 0..7 transform
 * the columns of `blk` into the rows of `z`, then the columns of `z` into
 * the rows of `blk`. Every thread of the group reaches both barriers. */
inline void hvs_fdct8x8(threadgroup int *blk, threadgroup int *z, uint lane)
{
    if (lane < 8u) {
        VmafMtlHvsLine column;
        for (uint r = 0u; r < 8u; ++r) {
            column.v[r] = blk[r * 8u + lane];
        }
        const VmafMtlHvsLine out = vmaf_mtl_hvs_fdct8(column);
        for (uint r = 0u; r < 8u; ++r) {
            z[lane * 8u + r] = out.v[r];
        }
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (lane < 8u) {
        VmafMtlHvsLine column;
        for (uint r = 0u; r < 8u; ++r) {
            column.v[r] = z[r * 8u + lane];
        }
        const VmafMtlHvsLine out = vmaf_mtl_hvs_fdct8(column);
        for (uint r = 0u; r < 8u; ++r) {
            blk[lane * 8u + r] = out.v[r];
        }
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
}

/* calc_psnrhvs()'s variance ratio of one image of the block (s_gvar after
 * its last assignment), from the raw samples. */
inline float hvs_variance_ratio(const threadgroup int *blk)
{
    VmafMtlHvsMoments sums = vmaf_mtl_hvs_moments_zero();
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            sums = vmaf_mtl_hvs_mean_add(sums, i, j, blk[i * 8 + j]);
        }
    }
    const VmafMtlHvsMoments means = vmaf_mtl_hvs_means(sums);
    VmafMtlHvsMoments variances = vmaf_mtl_hvs_moments_zero();
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            variances = vmaf_mtl_hvs_variance_add(variances, means, i, j, blk[i * 8 + j]);
        }
    }
    return vmaf_mtl_hvs_variance_ratio(variances);
}

/* calc_psnrhvs()'s masking energy of one image (s_mask before the root),
 * from its DCT coefficients, DC skipped. */
inline float hvs_mask_energy(const threadgroup int *coef, const constant float *mask)
{
    float energy = 0.f;
    for (uint i = 0u; i < 8u; ++i) {
        for (uint j = (i == 0u) ? 1u : 0u; j < 8u; ++j) {
            energy = vmaf_mtl_hvs_energy_add(energy, coef[i * 8u + j], mask[i * 8u + j]);
        }
    }
    return energy;
}

/* The 64 terms of one block, from its raw samples in `s_ref` / `s_dist`
 * (transformed in place). Every thread stores the term of its coefficient
 * at terms[slot * 64 + local_idx]: no sum on the device. */
inline void psnr_hvs_block_terms(threadgroup int *s_ref, threadgroup int *s_dist,
                                 threadgroup int *z_s, threadgroup int *z_d,
                                 threadgroup float *s_threshold, const constant float *csf,
                                 const constant float *mask, device float *terms, uint slot,
                                 uint lid, bool valid_block)
{
    float ratio_s = 0.f;
    float ratio_d = 0.f;
    if (lid == 0u) {
        ratio_s = hvs_variance_ratio(s_ref);
        ratio_d = hvs_variance_ratio(s_dist);
    }

    /* The first barrier inside waits for thread 0's ratios before pass 2
     * overwrites the samples. */
    hvs_fdct8x8(s_ref, z_s, lid);
    hvs_fdct8x8(s_dist, z_d, lid);

    if (lid == 0u) {
        const float threshold_s = vmaf_mtl_hvs_threshold(hvs_mask_energy(s_ref, mask), ratio_s);
        const float threshold_d = vmaf_mtl_hvs_threshold(hvs_mask_energy(s_dist, mask), ratio_d);
        *s_threshold = vmaf_mtl_hvs_block_threshold(threshold_s, threshold_d);
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

    const float term =
        vmaf_mtl_hvs_term(s_ref[lid], s_dist[lid], csf[lid], mask[lid], *s_threshold, lid);
    terms[(ulong)slot * VMAF_MTL_HVS_TERMS + lid] = valid_block ? term : 0.f;
}

/* ------------------------------------------------------------------ */
/*  8 bpc kernel                                                        */
/* ------------------------------------------------------------------ */
kernel void integer_psnr_hvs_8bpc(
    const device uchar *ref [[buffer(0)]], const device uchar *dis [[buffer(1)]],
    device float *terms [[buffer(2)]], const constant float *csf [[buffer(3)]],
    constant uint4 &dims [[buffer(4)]], constant uint2 &strides [[buffer(5)]],
    const constant float *mask [[buffer(6)]], uint2 bid [[threadgroup_position_in_grid]],
    uint2 lpos [[thread_position_in_threadgroup]], uint local_idx [[thread_index_in_threadgroup]])
{
    threadgroup int s_ref[64];
    threadgroup int s_dist[64];
    threadgroup int z_s[64];
    threadgroup int z_d[64];
    threadgroup float s_threshold;

    const uint width = dims.x;
    const uint height = dims.y;
    const uint num_blocks_x = dims.z;
    const uint num_blocks_y = dims.w;

    const uint blk_x = bid.x;
    const uint blk_y = bid.y;
    const uint lx = lpos.x;
    const uint ly = lpos.y;

    const uint x0 = blk_x * 7u;
    const uint y0 = blk_y * 7u;
    const bool valid_block =
        (blk_x < num_blocks_x && blk_y < num_blocks_y && x0 + 7u < width && y0 + 7u < height);

    int my_ref = 0;
    int my_dist = 0;
    if (valid_block) {
        const uint sx = x0 + lx;
        const uint sy = y0 + ly;
        my_ref = (int)ref[sy * strides.x + sx];
        my_dist = (int)dis[sy * strides.y + sx];
    }
    s_ref[local_idx] = my_ref;
    s_dist[local_idx] = my_dist;
    threadgroup_barrier(mem_flags::mem_threadgroup);

    const uint slot = blk_y * num_blocks_x + blk_x;
    psnr_hvs_block_terms(s_ref, s_dist, z_s, z_d, &s_threshold, csf, mask, terms, slot, local_idx,
                         valid_block);
}

/* ------------------------------------------------------------------ */
/*  16 bpc kernel                                                       */
/* ------------------------------------------------------------------ */
kernel void integer_psnr_hvs_16bpc(
    const device uchar *ref [[buffer(0)]], const device uchar *dis [[buffer(1)]],
    device float *terms [[buffer(2)]], const constant float *csf [[buffer(3)]],
    constant uint4 &dims [[buffer(4)]], constant uint2 &strides [[buffer(5)]],
    const constant float *mask [[buffer(6)]], uint2 bid [[threadgroup_position_in_grid]],
    uint2 lpos [[thread_position_in_threadgroup]], uint local_idx [[thread_index_in_threadgroup]])
{
    threadgroup int s_ref[64];
    threadgroup int s_dist[64];
    threadgroup int z_s[64];
    threadgroup int z_d[64];
    threadgroup float s_threshold;

    const uint width = dims.x;
    const uint height = dims.y;
    const uint num_blocks_x = dims.z;
    const uint num_blocks_y = dims.w;

    const uint blk_x = bid.x;
    const uint blk_y = bid.y;
    const uint lx = lpos.x;
    const uint ly = lpos.y;

    const uint x0 = blk_x * 7u;
    const uint y0 = blk_y * 7u;
    const bool valid_block =
        (blk_x < num_blocks_x && blk_y < num_blocks_y && x0 + 7u < width && y0 + 7u < height);

    int my_ref = 0;
    int my_dist = 0;
    if (valid_block) {
        const uint sx = x0 + lx;
        const uint sy = y0 + ly;
        /* Raw 9- to 12-bit integer samples (no scaler division) — matches
         * the CPU reference, which feeds full-range values into the DCT
         * and divides the final score by samplemax^2. */
        const device ushort *ref_row = (const device ushort *)(ref + sy * strides.x);
        const device ushort *dis_row = (const device ushort *)(dis + sy * strides.y);
        my_ref = (int)ref_row[sx];
        my_dist = (int)dis_row[sx];
    }
    s_ref[local_idx] = my_ref;
    s_dist[local_idx] = my_dist;
    threadgroup_barrier(mem_flags::mem_threadgroup);

    const uint slot = blk_y * num_blocks_x + blk_x;
    psnr_hvs_block_terms(s_ref, s_dist, z_s, z_d, &s_threshold, csf, mask, terms, slot, local_idx,
                         valid_block);
}
