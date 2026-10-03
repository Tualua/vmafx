/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

#ifndef FEATURE_FLOAT_MOMENT_SUM_GPU_H_
#define FEATURE_FLOAT_MOMENT_SUM_GPU_H_

/*
 * The four kernels that replace a float_moment twin's second-moment sums
 * with the CPU's past 2^53 units (ADR-1497), in the CUDA / HIP dialect both
 * twins compile: moment_score.cu and moment_score.hip include this header
 * after defining VMAF_ORDSUM_FUNC, VMAF_ORDSUM_NO_FP64 and
 * VMAF_MOMENT_SQUARE(v) (their moment_float_square()) and including
 * feature/float_moment_sum.h, and define four extern "C" __global__ kernels
 * of the names below, each calling its `_body` here, so that the module
 * lookup finds them. The arithmetic and every lane's steps are
 * float_moment_sum.h's; this header only lays them out over work-groups of
 * VMAF_MOMENT_SUM_LANES lanes with barriers:
 *
 *   moment_row_totals      each row's exact sum of float squares;
 *   moment_row_plans       a plan per row from the prefix of those sums;
 *   moment_row_units       each planned row's increments, composed in pixel
 *                          order over the lanes' runs and an ordered tree;
 *   moment_ordered_totals  one walk per plane over the rows. A row whose
 *                          plan does not hold at the CPU's sum is added as
 *                          its runs, and a run that crosses a binade term by
 *                          term, in pixel order.
 *
 * Each kernel returns at once when the plane's exact sum, which the frame
 * kernel leaves in the accumulator, is at most 2^53 units: then it is the
 * CPU's sum. The host launches them on the frame kernel's stream, after it:
 * moment_row_totals and moment_row_units over (height, planes) work-groups,
 * the other two over (planes) work-groups.
 */

/* The plane's exact sum is at most 2^53 units: it is the CPU's sum. */
static __device__ __forceinline__ bool moment_sum_is_exact(const VmafMomentSumArgs &a,
                                                           unsigned plane)
{
    return a.sums[2u + plane] <= VMAF_MOMENT_SUM_EXACT_END;
}

static __device__ __forceinline__ const uint16_t *moment_sum_row(const VmafMomentSumArgs &a,
                                                                 unsigned plane, unsigned row)
{
    return vmaf_moment_sum_line(a.luma[plane], a.stride[plane], row);
}

/* Each row's exact sum: blockIdx.x = row, blockIdx.y = plane. */
static __device__ __forceinline__ void moment_row_totals_body(const VmafMomentSumArgs &a)
{
    __shared__ uint64_t totals[VMAF_MOMENT_SUM_LANES];
    const unsigned plane = blockIdx.y;
    const unsigned row = blockIdx.x;
    const unsigned lane = threadIdx.x;
    if (moment_sum_is_exact(a, plane))
        return;
    totals[lane] = vmaf_moment_sum_lane_total(moment_sum_row(a, plane, row), a.width, lane);
    __syncthreads();
    for (unsigned step = VMAF_MOMENT_SUM_LANES / 2u; step > 0u; step >>= 1u) {
        if (lane < step)
            totals[lane] += totals[lane + step];
        __syncthreads();
    }
    if (lane == 0u)
        a.row_totals[(size_t)plane * a.height + row] = totals[0];
}

/* The plan of every row of one plane, blockIdx.x = plane: the lanes stage a
 * batch of row sums, lane 0 follows their prefix, the lanes store the plans. */
static __device__ __forceinline__ void moment_row_plans_body(const VmafMomentSumArgs &a)
{
    __shared__ uint64_t staged_totals[VMAF_MOMENT_SUM_BATCH];
    __shared__ int staged_plans[VMAF_MOMENT_SUM_BATCH];
    const unsigned plane = blockIdx.x;
    const unsigned lane = threadIdx.x;
    if (moment_sum_is_exact(a, plane))
        return;
    const size_t base = (size_t)plane * a.height;
    uint64_t prefix = 0u;
    for (unsigned first = 0; first < a.height; first += VMAF_MOMENT_SUM_BATCH) {
        const unsigned count =
            a.height - first < VMAF_MOMENT_SUM_BATCH ? a.height - first : VMAF_MOMENT_SUM_BATCH;
        /* lane < count <= VMAF_MOMENT_SUM_BATCH, spelled out for the analyzer. */
        const bool staged = lane < count && lane < VMAF_MOMENT_SUM_BATCH;
        if (staged)
            staged_totals[lane] = a.row_totals[base + first + lane];
        __syncthreads();
        if (lane == 0u)
            vmaf_moment_sum_plan_batch(&prefix, staged_totals, staged_plans, count);
        __syncthreads();
        if (staged)
            a.plans[base + first + lane] = staged_plans[lane];
        __syncthreads();
    }
}

/* Each planned row's increments under its plan: a lane composes its run,
 * then the lanes are composed in order. blockIdx.x = row, blockIdx.y =
 * plane. A row without a binade plan gets zeros, which the walk never
 * uses, and its samples are not read. */
static __device__ __forceinline__ void moment_row_units_body(const VmafMomentSumArgs &a)
{
    __shared__ int64_t units[2u * VMAF_MOMENT_SUM_LANES];
    const unsigned plane = blockIdx.y;
    const unsigned row = blockIdx.x;
    const unsigned lane = threadIdx.x;
    if (moment_sum_is_exact(a, plane))
        return;
    const size_t at = (size_t)plane * a.height + row;
    const int plan = a.plans[at];
    if (!vmaf_moment_sum_plan_is_binade(plan)) {
        if (lane == 0u) {
            a.row_units[(size_t)2u * at] = 0;
            a.row_units[((size_t)2u * at) + 1u] = 0;
        }
        return;
    }
    vmaf_moment_sum_lane_units(moment_sum_row(a, plane, row), a.width, plan, lane, units);
    __syncthreads();
    for (unsigned step = 1u; step < VMAF_MOMENT_SUM_LANES; step <<= 1u) {
        vmaf_moment_sum_tree_step(units, lane, step);
        __syncthreads();
    }
    if (lane == 0u) {
        a.row_units[(size_t)2u * at] = units[0];
        a.row_units[((size_t)2u * at) + 1u] = units[1];
    }
}

/* The CPU's second-moment sum of one plane, blockIdx.x = plane. Lane 0 walks
 * the rows, a staged batch at a time. When a row must be added as runs,
 * every lane computes one into shared memory and lane 0 adds them in order.
 * Every round stages a batch or consumes a row, which bounds the loop. */
static __device__ __forceinline__ void moment_ordered_totals_body(const VmafMomentSumArgs &a)
{
    __shared__ int staged_plans[VMAF_MOMENT_SUM_BATCH];
    __shared__ uint64_t staged_totals[VMAF_MOMENT_SUM_BATCH];
    __shared__ int64_t staged_units[2u * VMAF_MOMENT_SUM_BATCH];
    __shared__ uint64_t run_totals[VMAF_MOMENT_SUM_LANES];
    __shared__ int64_t run_low[2u * VMAF_MOMENT_SUM_LANES];
    __shared__ int64_t run_high[2u * VMAF_MOMENT_SUM_LANES];
    __shared__ unsigned command;
    __shared__ unsigned operand;
    __shared__ uint64_t walked;
    const unsigned plane = blockIdx.x;
    const unsigned lane = threadIdx.x;
    if (moment_sum_is_exact(a, plane))
        return;
    const size_t base = (size_t)plane * a.height;
    if (lane == 0u) {
        command = VMAF_MOMENT_WALK_LOAD;
        operand = 0u;
        walked = 0u;
    }
    uint64_t sum = 0u;
    unsigned row = 0u;
    unsigned first = 0u;
    const unsigned rounds = a.height + a.height / VMAF_MOMENT_SUM_BATCH + 2u;
    for (unsigned round = 0; round < rounds; round++) {
        __syncthreads();
        const unsigned todo = command;
        const unsigned what = operand;
        if (todo == VMAF_MOMENT_WALK_DONE)
            break;
        if (todo == VMAF_MOMENT_WALK_LOAD) {
            vmaf_moment_sum_walk_stage(
                a.plans + base, a.row_totals + base, a.row_units + (2u * base), a.height,
                what * VMAF_MOMENT_SUM_BATCH, lane, staged_plans, staged_totals, staged_units);
        } else {
            vmaf_moment_sum_walk_run(moment_sum_row(a, plane, what), a.width, walked, lane,
                                     run_totals, run_low, run_high);
        }
        __syncthreads();
        if (lane != 0u)
            continue;
        if (todo == VMAF_MOMENT_WALK_RUNS) {
            sum = vmaf_moment_sum_walk_row_runs(moment_sum_row(a, plane, what), a.width, sum,
                                                run_totals, run_low, run_high);
            row = what + 1u;
        } else {
            first = what * VMAF_MOMENT_SUM_BATCH;
        }
        unsigned next = 0u;
        command = vmaf_moment_sum_walk_next(a.height, first, &sum, &row, staged_plans,
                                            staged_totals, staged_units, &next);
        operand = next;
        walked = sum;
    }
    if (lane == 0u)
        a.sums[2u + plane] = sum;
}

#endif /* FEATURE_FLOAT_MOMENT_SUM_GPU_H_ */
