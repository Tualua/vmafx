/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

#ifndef FEATURE_FLOAT_MOMENT_SUM_H_
#define FEATURE_FLOAT_MOMENT_SUM_H_

/*
 * The sum moment.c::compute_2nd_moment() forms, with its bits, from pieces a
 * device forms in parallel (ADR-1497). Shared by the CUDA, SYCL and HIP
 * float_moment twins and by test_float_moment_sum.
 *
 * The CPU's loop is `cum += (double)term`, one float square per pixel in
 * raster order. In units of 1 / scaler^2 every term is an integer below 2^32
 * (the twins' moment_float_square()), and the loop is the same in units: a
 * power-of-two scale does not change where a double rounds. So the running
 * sum is an integer at every step, and each step is the exact integer sum
 * rounded to 53 significant bits, to nearest, a tie to the even value
 * (vmaf_moment_sum_add_term()). Below 2^53 nothing rounds and the exact
 * integer sum is the CPU's sum; that covers every frame of up to 2^21 pixels
 * at 16 bits and every frame at 8, 10 and 12 bits up to 2^29 pixels. Past
 * 2^53 the order of the adds matters.
 *
 * A twin forms that sum from rows, with the arithmetic of
 * feature/ordered_sum.h (ADR-1433) on integer terms:
 *
 *   1. the exact integer sum of each row's terms;
 *   2. a plan per row from the exact prefix of those sums
 *      (vmaf_moment_sum_plan_batch()): the exact range, or the binade the row
 *      is expected to start and end in;
 *   3. for a row with a binade, its terms' increments in that binade composed
 *      in pixel order (vmaf_moment_sum_term_units(), vmaf_ordsum_then(),
 *      vmaf_moment_sum_tree_step());
 *   4. one walk over the rows (vmaf_moment_sum_walk_rows()). A row the walk
 *      cannot add at the exact sum (it crosses a binade, or its plan does not
 *      hold there) is cut into VMAF_MOMENT_SUM_LANES runs, each added the
 *      same way under the binade the sum is in or the next one
 *      (vmaf_moment_sum_walk_runs()), and a run that crosses a binade is
 *      added term by term (vmaf_moment_sum_add_term()).
 *
 * The plan is advice: the walk checks it at the exact sum and falls back, so
 * a wrong plan costs time and never changes the result. A row adds less than
 * 2^47 units (at most 2^15 terms below 2^32) and a binade from 2^53 up spans
 * at least 2^53, so a row crosses at most one binade, and a frame's sum,
 * below 2^62, crosses at most nine.
 *
 * Integers only, for every backend (a SYCL kernel has no fp64, ADR-0220). A
 * caller that runs these in device code defines VMAF_ORDSUM_FUNC and
 * VMAF_ORDSUM_NO_FP64 before including the header, as for ordered_sum.h.
 * Every sum must stay below 2^63: a frame has at most 2^30 pixels
 * (VMAF_PIC_DIM_MAX), so the sums stay below 2^62.
 */

#include <stddef.h>
#include <stdint.h>

#include "ordered_sum.h"

/* The first sum at which an add can round: 2^53. */
#define VMAF_MOMENT_SUM_EXACT_END ((uint64_t)1 << 53u)
/* Binades a sum of float squares can reach: [2^53, 2^63). */
#define VMAF_MOMENT_SUM_MIN_BINADE 53
#define VMAF_MOMENT_SUM_MAX_BINADE 62
/* Plan of a row that ends at or below 2^53: its terms add exactly. A binade
 * plan is in [VMAF_MOMENT_SUM_MIN_BINADE, VMAF_MOMENT_SUM_MAX_BINADE], and a
 * row the plan expects to cross a binade is VMAF_ORDSUM_PLAN_TERMS. */
#define VMAF_MOMENT_PLAN_EXACT 0
/* Upper bound of vmaf_moment_sum_shift(): a uint64_t has 64 bits. */
#define VMAF_MOMENT_SUM_SHIFT_MAX 11u
/* Runs per row (one per lane of a work-group) and rows a walk stages at a
 * time. */
#define VMAF_MOMENT_SUM_LANES 256u
#define VMAF_MOMENT_SUM_BATCH 256u
/* The planes whose second moments the CPU's sum may round: ref, dis. */
#define VMAF_MOMENT_SUM_PLANES 2u
/* What lane 0 of a walk asks its work-group to do next. */
#define VMAF_MOMENT_WALK_LOAD 0u /* stage the batch of rows `operand` */
#define VMAF_MOMENT_WALK_RUNS 1u /* compute the runs of row `operand` */
#define VMAF_MOMENT_WALK_DONE 2u

/* What the four kernels of a twin read and write: the 16-bit luma planes,
 * three arrays of `height` rows per plane, and the frame's four accumulators
 * (ref1, dis1, ref2, dis2), whose entries 2 + plane the walk replaces with
 * the CPU's sums. Device addresses. */
struct VmafMomentSumArgs {
    const uint8_t *luma[VMAF_MOMENT_SUM_PLANES];
    size_t stride[VMAF_MOMENT_SUM_PLANES]; /* bytes */
    unsigned width;
    unsigned height;
    uint64_t *row_totals; /* [plane][row]: the exact sum of the row's terms */
    int *plans;           /* [plane][row]: vmaf_moment_sum_plan_batch() */
    int64_t *row_units;   /* [plane][row][2]: increments under the plan, even, odd */
    uint64_t *sums;       /* the frame's four accumulators */
};
#ifndef __cplusplus
typedef struct VmafMomentSumArgs VmafMomentSumArgs;
#endif

/* 1 when a frame's sum of float squares can pass 2^53 units: every term is
 * below 2^(2 * bpc). Only such frames need anything beyond the exact integer
 * sum. */
VMAF_ORDSUM_FUNC int vmaf_moment_sum_may_round(unsigned w, unsigned h, unsigned bpc)
{
    if (bpc <= 8u || bpc > 16u)
        return 0;
    return (uint64_t)w * (uint64_t)h > (VMAF_MOMENT_SUM_EXACT_END >> (2u * bpc));
}

/* The right shift that brings `x` below 2^53: 0 below 2^53, else the bit
 * length of x minus 53. */
VMAF_ORDSUM_FUNC unsigned vmaf_moment_sum_shift(uint64_t x)
{
    unsigned shift = 0u;
    for (unsigned i = 0u; i < VMAF_MOMENT_SUM_SHIFT_MAX; i++) {
        if ((x >> shift) < VMAF_MOMENT_SUM_EXACT_END)
            break;
        shift++;
    }
    return shift;
}

/* The binade [2^e, 2^(e+1)) of a sum at or above 2^53; VMAF_MOMENT_PLAN_EXACT
 * below it. */
VMAF_ORDSUM_FUNC int vmaf_moment_sum_binade(uint64_t sum)
{
    if (sum < VMAF_MOMENT_SUM_EXACT_END)
        return VMAF_MOMENT_PLAN_EXACT;
    return 52 + (int)vmaf_moment_sum_shift(sum);
}

/* 1 for a plan that names a binade. */
VMAF_ORDSUM_FUNC int vmaf_moment_sum_plan_is_binade(int plan)
{
    return plan >= VMAF_MOMENT_SUM_MIN_BINADE && plan <= VMAF_MOMENT_SUM_MAX_BINADE;
}

/* `sum + term` as the CPU's `cum += (double)term` forms it: the exact integer
 * sum rounded to 53 significant bits, to nearest, a tie to the even value
 * (the increment vmaf_ordsum_round_shifted() gives an even start). */
VMAF_ORDSUM_FUNC uint64_t vmaf_moment_sum_add_term(uint64_t sum, uint32_t term)
{
    const uint64_t exact = sum + (uint64_t)term;
    const unsigned shift = vmaf_moment_sum_shift(exact);
    if (shift == 0u)
        return exact;
    return (uint64_t)vmaf_ordsum_round_shifted(exact, (int)shift).even << shift;
}

/* Plan of a row from the exact integer sums before and after it: the exact
 * range when it ends at or below 2^53, the binade it starts and ends in, or
 * VMAF_ORDSUM_PLAN_TERMS. The CPU's sum differs from the exact one past 2^53,
 * so this is advice that the walk checks. */
VMAF_ORDSUM_FUNC int vmaf_moment_sum_plan(uint64_t before, uint64_t after)
{
    if (after <= VMAF_MOMENT_SUM_EXACT_END)
        return VMAF_MOMENT_PLAN_EXACT;
    const int binade = vmaf_moment_sum_binade(before);
    if (binade == VMAF_MOMENT_PLAN_EXACT || binade != vmaf_moment_sum_binade(after))
        return VMAF_ORDSUM_PLAN_TERMS;
    return binade;
}

/* Plans of `count` consecutive rows from their exact sums `totals`;
 * `*prefix` is the exact sum of every row before them and is advanced. */
VMAF_ORDSUM_FUNC void vmaf_moment_sum_plan_batch(uint64_t *prefix, const uint64_t *totals,
                                                 int *plans, unsigned count)
{
    uint64_t before = *prefix;
    for (unsigned i = 0; i < count && i < VMAF_MOMENT_SUM_BATCH; i++) {
        const uint64_t after = before + totals[i];
        plans[i] = vmaf_moment_sum_plan(before, after);
        before = after;
    }
    *prefix = before;
}

/* Increment of one term for a running sum in binade `plan`: the term in units
 * of 2^(plan - 52), rounded to nearest, a tie to the value that makes the
 * running integer even. Zero under a plan that is not a binade. */
VMAF_ORDSUM_FUNC VmafOrdsumUnits vmaf_moment_sum_term_units(uint32_t term, int plan)
{
    if (!vmaf_moment_sum_plan_is_binade(plan))
        return vmaf_ordsum_units(0, 0);
    return vmaf_ordsum_round_shifted((uint64_t)term, plan - 52);
}

/* The increments `units` (an even and an odd one per entry) of runs held in
 * lane order: at `step` (1, 2, 4, ...) every lane whose index is a multiple
 * of 2 * step takes the run `step` lanes above it, which finished the step
 * before. After the steps up to VMAF_MOMENT_SUM_LANES / 2, entry 0 holds the
 * increment of all the runs in order. The composition is not commutative,
 * so the pairing must be adjacent. The caller separates the steps with a
 * barrier. */
VMAF_ORDSUM_FUNC void vmaf_moment_sum_tree_step(int64_t *units, unsigned lane, unsigned step)
{
    if ((lane & (2u * step - 1u)) != 0u || lane + step >= VMAF_MOMENT_SUM_LANES)
        return;
    const VmafOrdsumUnits left =
        vmaf_ordsum_units(units[(size_t)2u * lane], units[((size_t)2u * lane) + 1u]);
    const unsigned right_lane = lane + step;
    const VmafOrdsumUnits right =
        vmaf_ordsum_units(units[(size_t)2u * right_lane], units[((size_t)2u * right_lane) + 1u]);
    const VmafOrdsumUnits both = vmaf_ordsum_then(left, right);
    units[(size_t)2u * lane] = both.even;
    units[((size_t)2u * lane) + 1u] = both.odd;
}

/* Columns [*first, *end) of run `lane` of a row of `width` terms: the row cut
 * into VMAF_MOMENT_SUM_LANES runs of equal length, the last ones shorter or
 * empty. */
VMAF_ORDSUM_FUNC void vmaf_moment_sum_run_bounds(unsigned width, unsigned lane, unsigned *first,
                                                 unsigned *end)
{
    const unsigned len = (width + VMAF_MOMENT_SUM_LANES - 1u) / VMAF_MOMENT_SUM_LANES;
    const unsigned lo = lane * len;
    *first = lo < width ? lo : width;
    *end = lo + len < width ? lo + len : width;
}

/* Adds a run of terms (a row, or a part of one) to the CPU's running sum
 * `*sum`. `total` is the exact integer sum of the run's terms and `units`
 * their increments under `plan`, composed in pixel order. The run is added
 * exactly when the sum stays at or below 2^53, and from `units` when the sum
 * is in binade `plan` and the run does not take it past the binade's end.
 * Returns 1 when the run is added, 0 when the caller must add its terms one
 * by one (`*sum` is unchanged then). */
VMAF_ORDSUM_FUNC int vmaf_moment_sum_add_run(uint64_t *sum, int plan, uint64_t total,
                                             VmafOrdsumUnits units)
{
    const uint64_t s = *sum;
    if (s <= VMAF_MOMENT_SUM_EXACT_END && total <= VMAF_MOMENT_SUM_EXACT_END - s) {
        *sum = s + total;
        return 1;
    }
    if (!vmaf_moment_sum_plan_is_binade(plan) || vmaf_moment_sum_binade(s) != plan)
        return 0;
    /* s is a multiple of 2^shift, m in [2^52, 2^53), and the run's increments
     * are rounded for that grid (vmaf_ordsum_add_chunk_bits() on the bits of
     * an integer-valued sum). */
    const unsigned shift = (unsigned)(plan - 52);
    const uint64_t m = s >> shift;
    const uint64_t end = m + (uint64_t)((m & 1u) ? units.odd : units.even);
    /* The run may end on the binade's end, 2^53 multiples, and no further. */
    if (end > VMAF_MOMENT_SUM_EXACT_END)
        return 0;
    *sum = end << shift;
    return 1;
}

/* Adds staged rows to the CPU's running sum `*sum`, from row `*row` up to
 * `end`; row r is staged at index r - first (its plan, its exact sum and its
 * increments as an even and an odd entry). Returns 0 when every row up to
 * `end` is added, 1 when row `*row` must be added as runs. */
VMAF_ORDSUM_FUNC int vmaf_moment_sum_walk_rows(uint64_t *sum, unsigned *row, unsigned first,
                                               unsigned end, const int *plans,
                                               const uint64_t *totals, const int64_t *units)
{
    unsigned r = *row;
    int stopped = 0;
    for (unsigned n = 0; n < VMAF_MOMENT_SUM_BATCH && r < end; n++) {
        const unsigned i = r - first;
        const VmafOrdsumUnits u =
            vmaf_ordsum_units(units[(size_t)2u * i], units[((size_t)2u * i) + 1u]);
        if (!vmaf_moment_sum_add_run(sum, plans[i], totals[i], u)) {
            stopped = 1;
            break;
        }
        r++;
    }
    *row = r;
    return stopped;
}

/* The two binades the runs of a row keep increments for, from the sum the row
 * starts at: the binade it is in (VMAF_MOMENT_PLAN_EXACT below 2^53) and the
 * next one. A row crosses at most one binade. */
VMAF_ORDSUM_FUNC void vmaf_moment_sum_run_binades(uint64_t sum, int *low, int *high)
{
    const int binade = vmaf_moment_sum_binade(sum);
    *low = binade;
    *high = binade == VMAF_MOMENT_PLAN_EXACT ? VMAF_MOMENT_SUM_MIN_BINADE : binade + 1;
}

/* Adds the runs of a row to `*sum`, from run `*run` up to `count`; run i has
 * the exact sum totals[i] and the increments units_low / units_high (even
 * and odd entries) under the binades `low` and `high` of
 * vmaf_moment_sum_run_binades(). Returns 0 when every run is added, 1 when
 * run `*run` must be added term by term. */
VMAF_ORDSUM_FUNC int vmaf_moment_sum_walk_runs(uint64_t *sum, unsigned *run, unsigned count,
                                               int low, int high, const uint64_t *totals,
                                               const int64_t *units_low, const int64_t *units_high)
{
    unsigned i = *run;
    int stopped = 0;
    for (unsigned n = 0; n < VMAF_MOMENT_SUM_LANES && i < count; n++) {
        const int binade = vmaf_moment_sum_binade(*sum);
        const int64_t *units = binade == low ? units_low : units_high;
        /* Increments exist for the two binades only: under any other the
         * run is added term by term. */
        const int plan = (binade == low || binade == high) ? binade : VMAF_ORDSUM_PLAN_TERMS;
        const VmafOrdsumUnits u =
            vmaf_ordsum_units(units[(size_t)2u * i], units[((size_t)2u * i) + 1u]);
        if (!vmaf_moment_sum_add_run(sum, plan, totals[i], u)) {
            stopped = 1;
            break;
        }
        i++;
    }
    *run = i;
    return stopped;
}

#ifdef VMAF_MOMENT_SQUARE
/*
 * What one lane of a kernel does between two barriers, for a caller that
 * defines VMAF_MOMENT_SQUARE(v): the term of a raw 16-bit sample, its
 * backend's moment_float_square(). The CUDA and HIP kernels are
 * float_moment_sum_gpu.h, the SYCL ones integer_moment_sycl.cpp, and
 * test_float_moment_sum runs the same steps on the host.
 */

/* Row `row` of a plane that starts at `luma`, `stride` bytes per row. */
VMAF_ORDSUM_FUNC const uint16_t *vmaf_moment_sum_line(const uint8_t *luma, size_t stride,
                                                      unsigned row)
{
    return (const uint16_t *)(luma + ((size_t)row * stride));
}

/* Kernel 1, one lane: the exact sum of the terms of columns lane, lane +
 * VMAF_MOMENT_SUM_LANES, ... of a row. */
VMAF_ORDSUM_FUNC uint64_t vmaf_moment_sum_lane_total(const uint16_t *line, unsigned width,
                                                     unsigned lane)
{
    uint64_t total = 0u;
    for (unsigned x = lane; x < width; x += VMAF_MOMENT_SUM_LANES)
        total += (uint64_t)VMAF_MOMENT_SQUARE(line[x]);
    return total;
}

/* The exact sum and the increments under `plan` of columns [first, end) of a
 * row, composed in pixel order. */
VMAF_ORDSUM_FUNC uint64_t vmaf_moment_sum_run(const uint16_t *line, unsigned first, unsigned end,
                                              int plan, VmafOrdsumUnits *units)
{
    uint64_t total = 0u;
    VmafOrdsumUnits u = vmaf_ordsum_units(0, 0);
    for (unsigned x = first; x < end; x++) {
        const uint32_t term = (uint32_t)VMAF_MOMENT_SQUARE(line[x]);
        total += term;
        u = vmaf_ordsum_then(u, vmaf_moment_sum_term_units(term, plan));
    }
    *units = u;
    return total;
}

/* Kernel 3, one lane: the increments of its run of a row under `plan`, into
 * entries 2 * lane and 2 * lane + 1 of `units`. */
VMAF_ORDSUM_FUNC void vmaf_moment_sum_lane_units(const uint16_t *line, unsigned width, int plan,
                                                 unsigned lane, int64_t *units)
{
    unsigned first;
    unsigned end;
    vmaf_moment_sum_run_bounds(width, lane, &first, &end);
    VmafOrdsumUnits u;
    (void)vmaf_moment_sum_run(line, first, end, plan, &u);
    units[(size_t)2u * lane] = u.even;
    units[((size_t)2u * lane) + 1u] = u.odd;
}

/* Kernel 4, one lane: stages row `first + lane` of a plane's arrays (`rows`
 * rows) into entry `lane` of the batch. */
VMAF_ORDSUM_FUNC void vmaf_moment_sum_walk_stage(const int *row_plans, const uint64_t *row_totals,
                                                 const int64_t *row_units, unsigned rows,
                                                 unsigned first, unsigned lane, int *plans,
                                                 uint64_t *totals, int64_t *units)
{
    const unsigned row = first + lane;
    if (row >= rows)
        return;
    plans[lane] = row_plans[row];
    totals[lane] = row_totals[row];
    units[(size_t)2u * lane] = row_units[(size_t)2u * row];
    units[((size_t)2u * lane) + 1u] = row_units[((size_t)2u * row) + 1u];
}

/* Kernel 4, one lane, for a row added as runs: its run's exact sum and its
 * increments under the two binades of vmaf_moment_sum_run_binades() for the
 * sum `start` the row starts at. */
VMAF_ORDSUM_FUNC void vmaf_moment_sum_walk_run(const uint16_t *line, unsigned width, uint64_t start,
                                               unsigned lane, uint64_t *totals, int64_t *low_units,
                                               int64_t *high_units)
{
    int low;
    int high;
    vmaf_moment_sum_run_binades(start, &low, &high);
    unsigned first;
    unsigned end;
    vmaf_moment_sum_run_bounds(width, lane, &first, &end);
    VmafOrdsumUnits u;
    totals[lane] = vmaf_moment_sum_run(line, first, end, low, &u);
    low_units[(size_t)2u * lane] = u.even;
    low_units[((size_t)2u * lane) + 1u] = u.odd;
    (void)vmaf_moment_sum_run(line, first, end, high, &u);
    high_units[(size_t)2u * lane] = u.even;
    high_units[((size_t)2u * lane) + 1u] = u.odd;
}

/* Kernel 4, lane 0: adds a row to `sum` as its runs, from what every lane
 * staged with vmaf_moment_sum_walk_run(), and a run that crosses a binade
 * term by term, in pixel order. */
VMAF_ORDSUM_FUNC uint64_t vmaf_moment_sum_walk_row_runs(const uint16_t *line, unsigned width,
                                                        uint64_t sum, const uint64_t *totals,
                                                        const int64_t *low_units,
                                                        const int64_t *high_units)
{
    int low;
    int high;
    vmaf_moment_sum_run_binades(sum, &low, &high);
    unsigned run = 0u;
    for (unsigned n = 0; n < VMAF_MOMENT_SUM_LANES; n++) {
        if (!vmaf_moment_sum_walk_runs(&sum, &run, VMAF_MOMENT_SUM_LANES, low, high, totals,
                                       low_units, high_units))
            break;
        unsigned first;
        unsigned end;
        vmaf_moment_sum_run_bounds(width, run, &first, &end);
        for (unsigned x = first; x < end; x++)
            sum = vmaf_moment_sum_add_term(sum, (uint32_t)VMAF_MOMENT_SQUARE(line[x]));
        run++;
    }
    return sum;
}

/* Kernel 4, lane 0: walks the staged batch that starts at row `first` from
 * row `*row`, and returns what the work-group does next
 * (VMAF_MOMENT_WALK_*) with its operand. */
VMAF_ORDSUM_FUNC unsigned vmaf_moment_sum_walk_next(unsigned rows, unsigned first, uint64_t *sum,
                                                    unsigned *row, const int *plans,
                                                    const uint64_t *totals, const int64_t *units,
                                                    unsigned *operand)
{
    const unsigned end =
        rows - first < VMAF_MOMENT_SUM_BATCH ? rows : first + VMAF_MOMENT_SUM_BATCH;
    if (*row < end && vmaf_moment_sum_walk_rows(sum, row, first, end, plans, totals, units)) {
        *operand = *row;
        return VMAF_MOMENT_WALK_RUNS;
    }
    *operand = *row / VMAF_MOMENT_SUM_BATCH;
    return *row < rows ? VMAF_MOMENT_WALK_LOAD : VMAF_MOMENT_WALK_DONE;
}
#endif /* VMAF_MOMENT_SQUARE */

#endif /* FEATURE_FLOAT_MOMENT_SUM_H_ */
