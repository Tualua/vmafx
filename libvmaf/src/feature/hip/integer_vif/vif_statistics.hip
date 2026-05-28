/**
 *  Copyright 2016-2023 Netflix, Inc.
 *  Copyright 2021 NVIDIA Corporation.
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: BSD-3-Clause-Plus-Patent
 *
 *  HIP port of the integer VIF filter + statistics kernels.
 *
 *  Direct functional port of libvmaf/src/feature/cuda/integer_vif/filter1d.cu
 *  and vif_statistics.cuh, simplified to a scalar-per-thread layout that
 *  drops the CUDA shared-memory tiling but retains the wavefront-level
 *  reduction to match the CUDA twin's warp-reduce + single-atomicAdd pattern.
 *  Numerical output matches the CPU integer_vif.c reference within places=4
 *  (the ADR-0214 cross-backend parity gate).
 *
 *  Algorithmic correspondence to the CUDA twin (key change vs. the broken
 *  pre-ADR-0537 HIP kernel):
 *
 *    1. Filter half-widths now come from `vif_filter1d_width[scale]/2`,
 *       NOT from the kernel-name suffix (which encodes both fwidth_0 and
 *       fwidth_rd — two distinct widths the old code conflated).
 *         scale 0: fwidth=17, half=8
 *         scale 1: fwidth= 9, half=4
 *         scale 2: fwidth= 5, half=2
 *         scale 3: fwidth= 3, half=1
 *
 *    2. Filter coefficients are uploaded once at init() time to a device
 *       buffer (4*18 = 144 bytes total) and passed to every kernel launch
 *       as `const uint16_t *vif_filt_dev`.  The pre-ADR-0537 kernel was
 *       being handed a host pointer, which caused the GPU memory access
 *       fault that motivated this fix.
 *
 *    3. The scale-0/1/2 vertical+horizontal kernel pair additionally
 *       computes the downsampling residual (the `*_convol` channels in
 *       CUDA), writing the half-resolution ref/dis planes that the next
 *       scale consumes.  The pre-ADR-0537 kernel skipped this step
 *       entirely, leaving scales 1..3 reading uninitialised device memory.
 *
 *    4. The 8-bit horizontal kernel now invokes the same statistic
 *       calculation as the 16-bit horizontal kernels (the pre-ADR-0537
 *       kernel called it with a placeholder cross-term, producing
 *       wrong-by-construction scores even when it didn't crash).
 *
 *    5. (ADR-0552) Deterministic wavefront reduction replaces the
 *       pre-fix per-thread atomicAdd.  The CUDA twin reduces 7 int64_t
 *       accumulator fields across 32 threads per warp using
 *       __shfl_down_sync before a single atomicAdd per warp.  Without
 *       this, each of the 128 horizontal-pass threads issued its own
 *       atomicAdd: AMD hardware serialises the 64-thread wavefront into
 *       arbitrarily-ordered CAS retries whose non-deterministic
 *       interleaving introduces per-frame jitter of ~0.001-0.014 at the
 *       per-feature level, which the VMAF SVM amplifies by 1.2-2.1x per
 *       scale to produce ~0.031 VMAF-score divergence vs CPU.
 *
 *       HIP port of the warp-reduce (AMD wavefront = 64 lanes on GCN/RDNA):
 *         __shfl_down_sync (CUDA, 32 lanes, strides 16..1)
 *             → __shfl_xor (HIP, 64 lanes, strides 32..1)
 *       After the wavefront reduce, only lane 0 of each wavefront issues
 *       the atomicAdd.  With BLOCK_X=128 there are 2 wavefronts per row,
 *       so 2 atomics per field instead of 128.  The resulting field values
 *       are fully deterministic: integer addition is associative, the
 *       wavefront XOR-shuffle is a fixed binary tree, and lane-0-of-wavefront
 *       is always the first 64 lanes' canonical representative — no CAS
 *       races, no ordering jitter.
 *
 *  Block layout (kept simple — no shared-memory tiling):
 *    - Vertical pass: BLOCK_X=32, BLOCK_Y=8, scalar per thread.
 *    - Horizontal pass + accumulation: BLOCK_X=128, BLOCK_Y=1,
 *      scalar per thread, wavefront reduce → lane-0 atomicAdd.
 *
 *  Wavefront size: this kernel uses the device-side `warpSize` variable
 *  (the standard HIP/CUDA implicit kernel parameter that holds the actual
 *  hardware wavefront width at runtime) rather than a hardcoded constant.
 *  RDNA2/RDNA3 targets compiled for gfx1030/gfx103x default to wave32 mode
 *  in ROCm 5.x+; GCN/VEGA targets use wave64.  Using `warpSize` makes the
 *  kernel correct on both families without requiring per-target recompilation.
 *
 *  ADR-0688: `AMD_WAVEFRONT_SIZE 64` was the pre-fix constant.  On gfx1036
 *  (Raphael iGPU, spoofed as gfx1030 via HSA_OVERRIDE_GFX_VERSION=10.3.0)
 *  the hardware executes wave32; `__shfl_xor(lo, 32)` exchanged with a lane
 *  outside the 32-lane wavefront, writing corrupted values into the
 *  accumulator.  `integer_vif_scale0..3` collapsed to near-zero, pulling
 *  VMAF from 76.668 down to 57.335 (19-point divergence).  Fix: replace all
 *  uses of `AMD_WAVEFRONT_SIZE` with `(unsigned)warpSize`.
 *
 *  Second sub-fix (ADR-0688): carry-preserving int64 wavefront reduction.
 *  The prior split lo/hi XOR-reduce lost the carry from the low 32-bit word to
 *  the high word during independent per-half reductions.  For negative int64
 *  values (lo ≈ 2^32), every partial sum generates a carry that was silently
 *  dropped, corrupting all 7 accumulator fields even after the warpSize fix.
 *  Fixed by mirroring the CUDA twin's warp_reduce (cuda_helper.cuh): keep x
 *  as int64_t throughout, reconstruct the neighbour's int64 per step via two
 *  32-bit __shfl_xor calls and OR, then add it — 64-bit addition preserves the
 *  carry implicitly.  This is the actual root cause of the 57.335 divergence.
 */

#include <hip/hip_runtime.h>
#include <stdint.h>

#include "hip/integer_vif_hip.h"

/* Filter widths per scale — matches libvmaf/src/feature/integer_vif.h
 * `vif_filter1d_width[4] = {17, 9, 5, 3}`. */
#define VIF_FW_S0 17
#define VIF_FW_S1  9
#define VIF_FW_S2  5
#define VIF_FW_S3  3

/* --------------------------------------------------------------------------
 * Bit-manipulation helpers — identical to the CUDA twin's vif_statistics.cuh.
 * -------------------------------------------------------------------------- */

__device__ __forceinline__ static uint16_t get_best16_from32(uint32_t temp, int *x)
{
    int k = __clz(temp);
    k = 16 - k;
    temp = temp >> k;
    *x = -k;
    return (uint16_t)temp;
}

__device__ __forceinline__ static uint16_t get_best16_from64(uint64_t temp, int *x)
{
    int k = __clzll(temp);
    if (k > 48) {
        k -= 48;
        temp = temp << k;
        *x = k;
    } else if (k < 47) {
        k = 48 - k;
        temp = temp >> k;
        *x = -k;
    } else {
        *x = 0;
        if (temp >> 16) {
            temp = temp >> 1;
            *x = -1;
        }
    }
    return (uint16_t)temp;
}

__device__ __forceinline__ static int32_t log_generate(int i)
{
    return (int32_t)__float2int_rn(log2f((float)i) * 2048.0f);
}

/* --------------------------------------------------------------------------
 * Per-pixel VIF statistic accumulation — scalar variant matching the CPU
 * integer_vif.c lines 300–348 (and the CUDA twin's vif_statistic_calculation
 * function called on per-thread scalars).
 * -------------------------------------------------------------------------- */

__device__ __forceinline__ static void vif_statistic_one(
    uint32_t mu1, uint32_t mu2, uint32_t xx_filt, uint32_t yy_filt, uint32_t xy_filt,
    double vif_enhn_gain_limit, vif_accums_hip *acc)
{
    const int32_t sigma_nsq = 65536 << 1;

    uint32_t mu1_sq  = (uint32_t)((((uint64_t)mu1 * mu1) + 2147483648ULL) >> 32);
    uint32_t mu2_sq  = (uint32_t)((((uint64_t)mu2 * mu2) + 2147483648ULL) >> 32);
    uint32_t mu1_mu2 = (uint32_t)((((uint64_t)mu1 * mu2) + 2147483648ULL) >> 32);

    int32_t sigma1_sq = (int32_t)(xx_filt - mu1_sq);
    int32_t sigma2_sq = (int32_t)(yy_filt - mu2_sq);
    int32_t sigma12   = (int32_t)(xy_filt - mu1_mu2);

    if (sigma2_sq < 0) sigma2_sq = 0;

    if (sigma1_sq >= sigma_nsq) {
        uint32_t log_den_stage1 = (uint32_t)(sigma_nsq + sigma1_sq);
        int x;
        uint16_t log_den1 = get_best16_from32(log_den_stage1, &x);

        acc->num_x  += 1;
        acc->x      += x;
        int32_t den_val = log_generate(log_den1);

        if (sigma12 > 0 && sigma2_sq > 0) {
            const double eps = 65536 * 1.0e-10;
            double g = sigma12 / ((double)sigma1_sq + eps);
            int32_t sv_sq = (int32_t)(sigma2_sq - g * sigma12);
            if (sv_sq < 0) sv_sq = 0;
            if (g > vif_enhn_gain_limit) g = vif_enhn_gain_limit;

            uint32_t numer1     = (uint32_t)(sv_sq + sigma_nsq);
            int64_t  numer1_tmp = (int64_t)(g * g * sigma1_sq) + (int64_t)numer1;

            int x1, x2;
            uint16_t numlog = get_best16_from64((uint64_t)numer1_tmp, &x1);
            uint16_t denlog = get_best16_from64((uint64_t)numer1, &x2);
            acc->x2      += (x2 - x1);
            int64_t num_val = (int64_t)log_generate(numlog) - (int64_t)log_generate(denlog);
            acc->num_log += num_val;
            acc->den_log += den_val;
        } else {
            acc->den_log += den_val;
        }
    } else {
        acc->num_non_log += sigma2_sq;
        acc->den_non_log += 1;
    }
}

/* --------------------------------------------------------------------------
 * Boundary helper — replicate: clamp coordinate to [lo, hi].
 * Both the vertical and horizontal passes use this to match the CPU
 * reference's PADDING_SQ_DATA replicate-pad (integer_vif.h lines 95-128).
 * The remaining ~0.001-0.003 per-scale HIP vs CPU delta vs CUDA (which uses
 * reflect-101 boundary in its smem-tile load) comes from single-precision
 * log2f() rounding differences, not from this boundary treatment.
 * -------------------------------------------------------------------------- */

__device__ __forceinline__ static int clamp_i(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* --------------------------------------------------------------------------
 * Wavefront-level reduction for int64_t (ADR-0552, corrected ADR-0688).
 *
 * XOR-reduce (__shfl_xor) with strides warpSize/2 .. 1.  __shfl_xor is the
 * portable AMD intrinsic; AMD wavefronts are inherently lock-step so no
 * sync mask is needed.
 *
 * Carry-preserving implementation (ADR-0688 sub-fix 2):
 * The int64_t value is NOT split into independent lo/hi halves.  Splitting
 * and reducing halves independently loses the carry from the low 32-bit word
 * to the high 32-bit word, corrupting all int64 fields when the partial sums
 * have their high lo-bit set (which always happens for small negative values
 * like the per-pixel shift amounts in x, x2, etc.).  Instead, at each step
 * we reconstruct the neighbour lane's full int64 from two 32-bit shuffles and
 * add it to the running int64 — 64-bit addition is exact (carry implicit).
 * This mirrors the CUDA twin's warp_reduce in cuda_helper.cuh exactly.
 * -------------------------------------------------------------------------- */

/* ADR-0688: removed AMD_WAVEFRONT_SIZE compile-time constant; use runtime
 * `warpSize` instead so the kernel is correct on both wave32 (gfx1030/1036)
 * and wave64 (GCN/Vega) targets without recompilation.
 * The WARPS_PER_BLOCK value is not needed here (we use a direct atomicAdd per
 * wavefront lane-0 rather than a shared-memory partial-sum array).
 *
 * ADR-0688 sub-fix 2: carry-preserving int64 reduction.
 * The previous split lo/hi approach lost carries when the low 32 bits of the
 * accumulated sum overflowed, producing wrong results for negative int64 values
 * (which have lo ≈ 2^32, generating carries on every addition).
 *
 * The correct approach — mirroring the CUDA twin's warp_reduce in cuda_helper.cuh —
 * keeps `x` as int64_t throughout.  At each XOR-reduce step we fetch the
 * neighbour lane's full int64_t by shuffling lo and hi halves separately and
 * OR-ing them back, then ADD that to x (64-bit addition, so carry is implicit
 * in the int64 arithmetic — no carry is lost).  This exactly matches:
 *   x += int64_t(__shfl_down_sync(0xffffffff, x & 0xffffffff, i)) |
 *        int64_t(__shfl_down_sync(0xffffffff, x >> 32, i) << 32)
 * from the CUDA twin, translated to the HIP XOR-reduce idiom. */

__device__ __forceinline__ static int64_t wavefront_reduce_i64(int64_t x)
{
    for (unsigned stride = (unsigned)warpSize / 2u; stride > 0u; stride >>= 1u) {
        /* Fetch the neighbour's int64 via two 32-bit shuffles, then OR-reconstruct
         * the full 64-bit value and add it to x.  64-bit addition preserves the
         * carry from the low word to the high word — the carry is never lost. */
        int64_t neighbour = (int64_t)((uint64_t)(unsigned)__shfl_xor((int)(x & 0xffffffff), (int)stride) |
                                      ((uint64_t)(unsigned)__shfl_xor((int)((uint64_t)x >> 32), (int)stride) << 32));
        x += neighbour;
    }
    return x;
}

/* Reduce all 7 fields of a vif_accums_hip across the wavefront.
 * Called once per thread; only lane 0's returned values are meaningful.
 * Fields are reduced individually — no aliasing concerns. */
__device__ __forceinline__ static vif_accums_hip wavefront_reduce_accums(vif_accums_hip a)
{
    a.x           = wavefront_reduce_i64(a.x);
    a.x2          = wavefront_reduce_i64(a.x2);
    a.num_x       = wavefront_reduce_i64(a.num_x);
    a.num_log     = wavefront_reduce_i64(a.num_log);
    a.den_log     = wavefront_reduce_i64(a.den_log);
    a.num_non_log = wavefront_reduce_i64(a.num_non_log);
    a.den_non_log = wavefront_reduce_i64(a.den_non_log);
    return a;
}

extern "C" {

/* --------------------------------------------------------------------------
 * Vertical filter pass — 8-bit path (scale 0 only).
 * -------------------------------------------------------------------------- */

__global__ void filter1d_8_vertical_kernel_uint32_t_17_9(
    VifBufferHip buf,
    const uint8_t *__restrict__ ref_in,
    const uint8_t *__restrict__ dis_in,
    int w, int h,
    const uint16_t *__restrict__ vif_filt_dev)
{
    const int x = (int)(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = (int)(blockIdx.y * blockDim.y + threadIdx.y);
    if (x >= w || y >= h)
        return;

    const int FW = VIF_FW_S0;
    const int HALF = FW / 2;
    const int FW_RD = VIF_FW_S1;
    const int RD_START = (FW - FW_RD) / 2;

    const uint16_t *filt    = &vif_filt_dev[0 * 18];
    const uint16_t *filt_rd = &vif_filt_dev[1 * 18];

    uint32_t accum_mu1 = 0, accum_mu2 = 0;
    uint32_t accum_ref = 0, accum_dis = 0, accum_ref_dis = 0;
    uint32_t accum_ref_rd = 0, accum_dis_rd = 0;

    for (int fi = 0; fi < FW; ++fi) {
        int ky = clamp_i(y - HALF + fi, 0, h - 1);
        uint32_t r = ref_in[(size_t)ky * (size_t)buf.stride + (size_t)x];
        uint32_t d = dis_in[(size_t)ky * (size_t)buf.stride + (size_t)x];
        uint32_t coeff = filt[fi];
        uint32_t cr = coeff * r;
        uint32_t cd = coeff * d;
        accum_mu1 += cr;
        accum_mu2 += cd;
        accum_ref += cr * r;
        accum_dis += cd * d;
        accum_ref_dis += cr * d;

        if (fi >= RD_START && fi < RD_START + FW_RD) {
            uint32_t cr_rd = (uint32_t)filt_rd[fi - RD_START];
            accum_ref_rd += cr_rd * r;
            accum_dis_rd += cr_rd * d;
        }
    }

    const int stmp = (int)(buf.stride_tmp / sizeof(uint32_t));
    buf.tmp.mu1[y * stmp + x]     = (accum_mu1 + 128u) >> 8;
    buf.tmp.mu2[y * stmp + x]     = (accum_mu2 + 128u) >> 8;
    buf.tmp.ref[y * stmp + x]     = accum_ref;
    buf.tmp.dis[y * stmp + x]     = accum_dis;
    buf.tmp.ref_dis[y * stmp + x] = accum_ref_dis;
    buf.tmp.ref_convol[y * stmp + x] = (accum_ref_rd + 128u) >> 8;
    buf.tmp.dis_convol[y * stmp + x] = (accum_dis_rd + 128u) >> 8;
}

__global__ void filter1d_8_horizontal_kernel_2_17_9(
    VifBufferHip buf,
    int w, int h,
    const uint16_t *__restrict__ vif_filt_dev,
    double vif_enhn_gain_limit,
    vif_accums_hip *accum_out)
{
    const int x = (int)(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = (int)(blockIdx.y * blockDim.y + threadIdx.y);

    /* ADR-0552: do NOT early-return on out-of-bounds threads.  All threads in a
     * wavefront must reach the wavefront_reduce_accums() call together; an early
     * return diverges the wavefront and leaves __shfl_xor reading uninitialised
     * lanes.  Out-of-bounds threads contribute a zero-initialised thr struct,
     * which is neutral under integer addition — correct by construction. */

    const int FW = VIF_FW_S0;
    const int HALF = FW / 2;
    const int FW_RD = VIF_FW_S1;
    const int HALF_RD = FW_RD / 2;

    const uint16_t *filt    = &vif_filt_dev[0 * 18];
    const uint16_t *filt_rd = &vif_filt_dev[1 * 18];

    /* Zero-initialise so out-of-bounds threads contribute nothing. */
    vif_accums_hip thr = {0, 0, 0, 0, 0, 0, 0};

    if (x < w && y < h) {
        const int stmp = (int)(buf.stride_tmp / sizeof(uint32_t));
        const int row  = y * stmp;

        uint32_t accum_mu1 = 0, accum_mu2 = 0;
        uint64_t accum_ref = 0, accum_dis = 0, accum_ref_dis = 0;

        for (int fj = 0; fj < FW; ++fj) {
            int kx = clamp_i(x - HALF + fj, 0, w - 1);
            uint32_t coeff = (uint32_t)filt[fj];
            accum_mu1 += coeff * buf.tmp.mu1[row + kx];
            accum_mu2 += coeff * buf.tmp.mu2[row + kx];
            accum_ref += (uint64_t)coeff * (uint64_t)buf.tmp.ref[row + kx];
            accum_dis += (uint64_t)coeff * (uint64_t)buf.tmp.dis[row + kx];
            accum_ref_dis += (uint64_t)coeff * (uint64_t)buf.tmp.ref_dis[row + kx];
        }

        uint32_t mu1_v = accum_mu1;
        uint32_t mu2_v = accum_mu2;
        uint32_t xx_v  = (uint32_t)((accum_ref     + 32768) >> 16);
        uint32_t yy_v  = (uint32_t)((accum_dis     + 32768) >> 16);
        uint32_t xy_v  = (uint32_t)((accum_ref_dis + 32768) >> 16);

        vif_statistic_one(mu1_v, mu2_v, xx_v, yy_v, xy_v, vif_enhn_gain_limit, &thr);
    }

    /* ADR-0552: wavefront-level reduction before a single atomicAdd per wavefront.
     * ADR-0688: use runtime warpSize for the lane-0 check (was AMD_WAVEFRONT_SIZE=64,
     * incorrect on wave32 gfx1030/gfx1036 targets).  Only lane 0 of each wavefront
     * writes to global memory. */
    thr = wavefront_reduce_accums(thr);
    if ((threadIdx.x % (unsigned)warpSize) == 0u) {
        atomicAdd((unsigned long long *)&accum_out->x,           (unsigned long long)thr.x);
        atomicAdd((unsigned long long *)&accum_out->x2,          (unsigned long long)thr.x2);
        atomicAdd((unsigned long long *)&accum_out->num_x,       (unsigned long long)thr.num_x);
        atomicAdd((unsigned long long *)&accum_out->num_log,     (unsigned long long)thr.num_log);
        atomicAdd((unsigned long long *)&accum_out->den_log,     (unsigned long long)thr.den_log);
        atomicAdd((unsigned long long *)&accum_out->num_non_log, (unsigned long long)thr.num_non_log);
        atomicAdd((unsigned long long *)&accum_out->den_non_log, (unsigned long long)thr.den_non_log);
    }

    if (x < w && y < h && (y & 1) == 0 && (x & 1) == 0) {
        const int stmp = (int)(buf.stride_tmp / sizeof(uint32_t));
        const int row  = y * stmp;
        uint32_t accum_ref_rd = 0, accum_dis_rd = 0;
        for (int fj = 0; fj < FW_RD; ++fj) {
            int kx = clamp_i(x - HALF_RD + fj, 0, w - 1);
            uint32_t coeff = (uint32_t)filt_rd[fj];
            accum_ref_rd += coeff * buf.tmp.ref_convol[row + kx];
            accum_dis_rd += coeff * buf.tmp.dis_convol[row + kx];
        }
        uint16_t mu_ref = (uint16_t)((accum_ref_rd + 32768u) >> 16);
        uint16_t mu_dis = (uint16_t)((accum_dis_rd + 32768u) >> 16);

        uint16_t *rd_ref = (uint16_t *)buf.ref;
        uint16_t *rd_dis = (uint16_t *)buf.dis;
        const ptrdiff_t rd_stride16 = buf.rd_stride / (ptrdiff_t)sizeof(uint16_t);
        rd_ref[(y / 2) * rd_stride16 + (x / 2)] = mu_ref;
        rd_dis[(y / 2) * rd_stride16 + (x / 2)] = mu_dis;
    }
}


} /* extern "C" — templates need C++ linkage */

template <int FW, int FW_RD>
__device__ __forceinline__ static void vif_vert_16_body(
    VifBufferHip &buf, const uint16_t *ref_in, const uint16_t *dis_in,
    int w, int h, int scale,
    int32_t add_shift_VP, int32_t shift_VP,
    int32_t add_shift_VP_sq, int32_t shift_VP_sq,
    const uint16_t *filt, const uint16_t *filt_rd)
{
    const int x = (int)(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = (int)(blockIdx.y * blockDim.y + threadIdx.y);
    if (x >= w || y >= h)
        return;

    const int HALF    = FW / 2;
    const int RD_START = (FW - FW_RD) / 2;

    const ptrdiff_t in_stride16 = (scale == 0)
        ? (buf.stride    / (ptrdiff_t)sizeof(uint16_t))
        : (buf.rd_stride / (ptrdiff_t)sizeof(uint16_t));

    uint32_t accum_mu1 = 0, accum_mu2 = 0;
    uint64_t accum_ref = 0, accum_dis = 0, accum_ref_dis = 0;
    uint32_t accum_ref_rd = 0, accum_dis_rd = 0;

    for (int fi = 0; fi < FW; ++fi) {
        int ky = clamp_i(y - HALF + fi, 0, h - 1);
        uint32_t r = ref_in[(ptrdiff_t)ky * in_stride16 + x];
        uint32_t d = dis_in[(ptrdiff_t)ky * in_stride16 + x];
        uint32_t coeff = (uint32_t)filt[fi];
        uint32_t cr = coeff * r;
        uint32_t cd = coeff * d;
        accum_mu1 += cr;
        accum_mu2 += cd;
        accum_ref += (uint64_t)cr * r;
        accum_dis += (uint64_t)cd * d;
        accum_ref_dis += (uint64_t)cr * d;

        if (FW_RD > 0 && fi >= RD_START && fi < RD_START + FW_RD) {
            uint32_t cr_rd = (uint32_t)filt_rd[fi - RD_START];
            accum_ref_rd += cr_rd * r;
            accum_dis_rd += cr_rd * d;
        }
    }

    const int stmp = (int)(buf.stride_tmp / sizeof(uint32_t));
    buf.tmp.mu1[y * stmp + x]     = (accum_mu1 + (uint32_t)add_shift_VP) >> shift_VP;
    buf.tmp.mu2[y * stmp + x]     = (accum_mu2 + (uint32_t)add_shift_VP) >> shift_VP;
    buf.tmp.ref[y * stmp + x]     = (uint32_t)((accum_ref     + (uint64_t)add_shift_VP_sq) >> shift_VP_sq);
    buf.tmp.dis[y * stmp + x]     = (uint32_t)((accum_dis     + (uint64_t)add_shift_VP_sq) >> shift_VP_sq);
    buf.tmp.ref_dis[y * stmp + x] = (uint32_t)((accum_ref_dis + (uint64_t)add_shift_VP_sq) >> shift_VP_sq);
    if (FW_RD > 0) {
        buf.tmp.ref_convol[y * stmp + x] = (accum_ref_rd + (uint32_t)add_shift_VP) >> shift_VP;
        buf.tmp.dis_convol[y * stmp + x] = (accum_dis_rd + (uint32_t)add_shift_VP) >> shift_VP;
    }
}

template <int FW, int FW_RD>
__device__ __forceinline__ static void vif_hori_16_body(
    VifBufferHip &buf, int w, int h, int scale,
    int32_t add_shift_HP, int32_t shift_HP,
    const uint16_t *filt, const uint16_t *filt_rd,
    double vif_enhn_gain_limit,
    vif_accums_hip *accum_out)
{
    (void)scale;
    const int x = (int)(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = (int)(blockIdx.y * blockDim.y + threadIdx.y);

    /* ADR-0552: do NOT early-return on out-of-bounds threads.  All threads in a
     * wavefront must reach the wavefront_reduce_accums() call together; an early
     * return diverges the wavefront and leaves __shfl_xor reading uninitialised
     * lanes.  Out-of-bounds threads contribute a zero-initialised thr struct,
     * which is neutral under integer addition — correct by construction. */

    const int HALF    = FW / 2;
    const int HALF_RD = FW_RD / 2;

    /* stmp and row are safe to compute for all threads — clamp y to 0 for
     * out-of-bounds rows to avoid negative indexing; the resulting values
     * are never used (the if(x<w&&y<h) guard below and the FW_RD guard below
     * both exclude out-of-bounds threads from global memory writes). */
    const int stmp = (int)(buf.stride_tmp / sizeof(uint32_t));
    const int row  = clamp_i(y, 0, h > 0 ? h - 1 : 0) * stmp;

    /* Zero-initialise so out-of-bounds threads contribute nothing. */
    vif_accums_hip thr = {0, 0, 0, 0, 0, 0, 0};

    if (x < w && y < h) {
        uint32_t accum_mu1 = 0, accum_mu2 = 0;
        uint64_t accum_ref = 0, accum_dis = 0, accum_ref_dis = 0;

        for (int fj = 0; fj < FW; ++fj) {
            int kx = clamp_i(x - HALF + fj, 0, w - 1);
            uint32_t coeff = (uint32_t)filt[fj];
            accum_mu1 += coeff * buf.tmp.mu1[row + kx];
            accum_mu2 += coeff * buf.tmp.mu2[row + kx];
            accum_ref += (uint64_t)coeff * (uint64_t)buf.tmp.ref[row + kx];
            accum_dis += (uint64_t)coeff * (uint64_t)buf.tmp.dis[row + kx];
            accum_ref_dis += (uint64_t)coeff * (uint64_t)buf.tmp.ref_dis[row + kx];
        }

        uint32_t mu1_v = accum_mu1;
        uint32_t mu2_v = accum_mu2;
        uint32_t xx_v  = (uint32_t)((accum_ref     + (uint64_t)add_shift_HP) >> shift_HP);
        uint32_t yy_v  = (uint32_t)((accum_dis     + (uint64_t)add_shift_HP) >> shift_HP);
        uint32_t xy_v  = (uint32_t)((accum_ref_dis + (uint64_t)add_shift_HP) >> shift_HP);

        vif_statistic_one(mu1_v, mu2_v, xx_v, yy_v, xy_v, vif_enhn_gain_limit, &thr);
    }

    /* ADR-0552: wavefront-level reduction before a single atomicAdd per wavefront.
     * ADR-0688: use runtime warpSize for the lane-0 check.  Only lane 0 writes. */
    thr = wavefront_reduce_accums(thr);
    if ((threadIdx.x % (unsigned)warpSize) == 0u) {
        atomicAdd((unsigned long long *)&accum_out->x,           (unsigned long long)thr.x);
        atomicAdd((unsigned long long *)&accum_out->x2,          (unsigned long long)thr.x2);
        atomicAdd((unsigned long long *)&accum_out->num_x,       (unsigned long long)thr.num_x);
        atomicAdd((unsigned long long *)&accum_out->num_log,     (unsigned long long)thr.num_log);
        atomicAdd((unsigned long long *)&accum_out->den_log,     (unsigned long long)thr.den_log);
        atomicAdd((unsigned long long *)&accum_out->num_non_log, (unsigned long long)thr.num_non_log);
        atomicAdd((unsigned long long *)&accum_out->den_non_log, (unsigned long long)thr.den_non_log);
    }

    if (FW_RD > 0 && x < w && y < h && (y & 1) == 0 && (x & 1) == 0) {
        uint32_t accum_ref_rd = 0, accum_dis_rd = 0;
        for (int fj = 0; fj < FW_RD; ++fj) {
            int kx = clamp_i(x - HALF_RD + fj, 0, w - 1);
            uint32_t coeff = (uint32_t)filt_rd[fj];
            accum_ref_rd += coeff * buf.tmp.ref_convol[row + kx];
            accum_dis_rd += coeff * buf.tmp.dis_convol[row + kx];
        }
        uint16_t mu_ref = (uint16_t)((accum_ref_rd + 32768u) >> 16);
        uint16_t mu_dis = (uint16_t)((accum_dis_rd + 32768u) >> 16);

        uint16_t *rd_ref = (uint16_t *)buf.ref;
        uint16_t *rd_dis = (uint16_t *)buf.dis;
        const ptrdiff_t rd_stride16 = buf.rd_stride / (ptrdiff_t)sizeof(uint16_t);
        rd_ref[(y / 2) * rd_stride16 + (x / 2)] = mu_ref;
        rd_dis[(y / 2) * rd_stride16 + (x / 2)] = mu_dis;
    }
}


extern "C" {

__global__ void filter1d_16_vertical_kernel_uint2_17_9_0(
    VifBufferHip buf, const uint16_t *__restrict__ ref_in, const uint16_t *__restrict__ dis_in,
    int w, int h,
    int32_t add_shift_VP, int32_t shift_VP, int32_t add_shift_VP_sq, int32_t shift_VP_sq,
    const uint16_t *__restrict__ vif_filt_dev)
{
    vif_vert_16_body<VIF_FW_S0, VIF_FW_S1>(buf, ref_in, dis_in, w, h, 0,
        add_shift_VP, shift_VP, add_shift_VP_sq, shift_VP_sq,
        &vif_filt_dev[0 * 18], &vif_filt_dev[1 * 18]);
}

__global__ void filter1d_16_vertical_kernel_uint2_9_5_1(
    VifBufferHip buf, const uint16_t *__restrict__ ref_in, const uint16_t *__restrict__ dis_in,
    int w, int h,
    int32_t add_shift_VP, int32_t shift_VP, int32_t add_shift_VP_sq, int32_t shift_VP_sq,
    const uint16_t *__restrict__ vif_filt_dev)
{
    vif_vert_16_body<VIF_FW_S1, VIF_FW_S2>(buf, ref_in, dis_in, w, h, 1,
        add_shift_VP, shift_VP, add_shift_VP_sq, shift_VP_sq,
        &vif_filt_dev[1 * 18], &vif_filt_dev[2 * 18]);
}

__global__ void filter1d_16_vertical_kernel_uint2_5_3_2(
    VifBufferHip buf, const uint16_t *__restrict__ ref_in, const uint16_t *__restrict__ dis_in,
    int w, int h,
    int32_t add_shift_VP, int32_t shift_VP, int32_t add_shift_VP_sq, int32_t shift_VP_sq,
    const uint16_t *__restrict__ vif_filt_dev)
{
    vif_vert_16_body<VIF_FW_S2, VIF_FW_S3>(buf, ref_in, dis_in, w, h, 2,
        add_shift_VP, shift_VP, add_shift_VP_sq, shift_VP_sq,
        &vif_filt_dev[2 * 18], &vif_filt_dev[3 * 18]);
}

__global__ void filter1d_16_vertical_kernel_uint2_3_0_3(
    VifBufferHip buf, const uint16_t *__restrict__ ref_in, const uint16_t *__restrict__ dis_in,
    int w, int h,
    int32_t add_shift_VP, int32_t shift_VP, int32_t add_shift_VP_sq, int32_t shift_VP_sq,
    const uint16_t *__restrict__ vif_filt_dev)
{
    vif_vert_16_body<VIF_FW_S3, 0>(buf, ref_in, dis_in, w, h, 3,
        add_shift_VP, shift_VP, add_shift_VP_sq, shift_VP_sq,
        &vif_filt_dev[3 * 18], nullptr);
}

__global__ void filter1d_16_horizontal_kernel_2_17_9_0(
    VifBufferHip buf, int w, int h,
    int32_t add_shift_HP, int32_t shift_HP,
    const uint16_t *__restrict__ vif_filt_dev,
    double vif_enhn_gain_limit,
    vif_accums_hip *accum_out)
{
    vif_hori_16_body<VIF_FW_S0, VIF_FW_S1>(buf, w, h, 0,
        add_shift_HP, shift_HP,
        &vif_filt_dev[0 * 18], &vif_filt_dev[1 * 18],
        vif_enhn_gain_limit, accum_out);
}

__global__ void filter1d_16_horizontal_kernel_2_9_5_1(
    VifBufferHip buf, int w, int h,
    int32_t add_shift_HP, int32_t shift_HP,
    const uint16_t *__restrict__ vif_filt_dev,
    double vif_enhn_gain_limit,
    vif_accums_hip *accum_out)
{
    vif_hori_16_body<VIF_FW_S1, VIF_FW_S2>(buf, w, h, 1,
        add_shift_HP, shift_HP,
        &vif_filt_dev[1 * 18], &vif_filt_dev[2 * 18],
        vif_enhn_gain_limit, accum_out);
}

__global__ void filter1d_16_horizontal_kernel_2_5_3_2(
    VifBufferHip buf, int w, int h,
    int32_t add_shift_HP, int32_t shift_HP,
    const uint16_t *__restrict__ vif_filt_dev,
    double vif_enhn_gain_limit,
    vif_accums_hip *accum_out)
{
    vif_hori_16_body<VIF_FW_S2, VIF_FW_S3>(buf, w, h, 2,
        add_shift_HP, shift_HP,
        &vif_filt_dev[2 * 18], &vif_filt_dev[3 * 18],
        vif_enhn_gain_limit, accum_out);
}

__global__ void filter1d_16_horizontal_kernel_2_3_0_3(
    VifBufferHip buf, int w, int h,
    int32_t add_shift_HP, int32_t shift_HP,
    const uint16_t *__restrict__ vif_filt_dev,
    double vif_enhn_gain_limit,
    vif_accums_hip *accum_out)
{
    vif_hori_16_body<VIF_FW_S3, 0>(buf, w, h, 3,
        add_shift_HP, shift_HP,
        &vif_filt_dev[3 * 18], nullptr,
        vif_enhn_gain_limit, accum_out);
}

} /* extern "C" */
