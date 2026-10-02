/**
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: EUPL-1.2
 *
 *  The sum `for (i = 0; i < n; i++) s += x[i];` of non-negative doubles with
 *  the bits a sequential loop gives, for a SYCL twin (ADR-1446): what
 *  feature/ordered_sum.h (ADR-1433) needs around it on a device that has no
 *  fp64 type (ADR-0220) and whose single lanes are slow.
 *
 *  ordered_sum.h turns the loop into integer increments per chunk, valid
 *  while the running sum stays in one binade, and a walk over the chunks that
 *  checks each chunk's plan at the exact sum. This header has the three
 *  parts a twin adds:
 *
 *    - the plan from fp32 advice sums (plan_chunks): the binade each chunk is
 *      expected to start and end in. A chunk the advice sends to the
 *      term-by-term path gets a slot;
 *    - what is kept for a chunk with a slot (stage_run): its terms, and their
 *      increments composed into runs of kRun terms under the two binades
 *      the chunk is expected to end in. A chunk that crosses one binade is
 *      then runs, the terms of one run, and runs again, a few dozen steps on
 *      the walk's one lane where the terms alone are kChunk;
 *    - the walk (walk_sum): fp64 bit patterns and integers only.
 *
 *  Every plan is advice. The walk adds a chunk from its increment only when
 *  vmaf_ordsum_add_chunk_bits() accepts it at the exact sum, and a run
 *  likewise; everything else is added term by term with add_bits(), which is
 *  the fp64 addition. A wrong plan, a missing slot or a wrong expected binade
 *  costs time and never changes the result. test_sycl_ordered_sum checks
 *  that on the host with wrong plans of every kind.
 *
 *  Kernel-safe: integers and fp32 only, no arrays, every function always
 *  inlined (a call left in a kernel is a scratch-memory frame, ADR-1395).
 */

#ifndef VMAF_FEATURE_SYCL_SYCL_ORDERED_SUM_H_
#define VMAF_FEATURE_SYCL_SYCL_ORDERED_SUM_H_

#include <sycl/sycl.hpp>

#include <cstddef>
#include <cstdint>

#include "sycl_compat.h"
#include "sycl_soft_signed.h"

/* ordered_sum.h on fp64 bit patterns only. */
#define VMAF_ORDSUM_NO_FP64
#define VMAF_ORDSUM_FUNC VMAF_SYCL_ALWAYS_INLINE
#include "../ordered_sum.h"

namespace vmaf_sycl_ordsum
{

/* Terms per chunk: consecutive in the loop's order, one work-group. */
inline constexpr unsigned kChunk = 512;
/* Terms per run of a kept chunk, and runs per chunk. */
inline constexpr unsigned kRun = 16;
inline constexpr unsigned kRuns = kChunk / kRun;
/* int64 values a run keeps: [expected binade, the next one][even, odd]. */
inline constexpr unsigned kRunUnits = 4;
/* Chunks per sum that may have a slot. A sum crosses a binade a few dozen
 * times; a chunk past this count has its terms computed by the walk. */
inline constexpr int kSlotCodes = 128;
inline constexpr unsigned kSlots = kSlotCodes;
/* What the plan buffer holds for a chunk with a slot: a code below every
 * binade and above VMAF_ORDSUM_PLAN_TERMS. */
inline constexpr int kSlotCodeBase = VMAF_ORDSUM_PLAN_TERMS + 1;
/* The quiet NaN a sum that met one is returned as. */
inline constexpr uint64_t kNanBits = vmaf_sycl_soft::kQuietNanBits;

/* The plan a buffer entry stands for: a slot code is a chunk whose terms are
 * added one by one. */
VMAF_SYCL_ALWAYS_INLINE int plan_of(int16_t entry)
{
    const int value = (int)entry;
    const bool coded = value >= kSlotCodeBase && value < kSlotCodeBase + kSlotCodes;
    return coded ? VMAF_ORDSUM_PLAN_TERMS : value;
}

/* The slot a chunk's terms are kept in, or -1. */
VMAF_SYCL_ALWAYS_INLINE int slot_of(int16_t entry)
{
    const int slot = (int)entry - kSlotCodeBase;
    return (slot >= 0 && slot < kSlotCodes) ? slot : -1;
}

/* Binade of the exact sum an fp32 advice prefix stands for: the fp32 value's
 * own binade less `scale_log2`, the power of two the advice sums are scaled
 * by. VMAF_ORDSUM_PLAN_TERMS for a prefix that is not a positive normal
 * value. */
VMAF_SYCL_ALWAYS_INLINE int advice_binade(float prefix, int scale_log2)
{
    const auto bits = sycl::bit_cast<uint32_t>(prefix);
    const int field = (int)((bits >> 23) & 0xFFu);
    if (!(prefix > 0.0f) || field == 0 || field == 255)
        return VMAF_ORDSUM_PLAN_TERMS;
    return field - 127 - scale_log2;
}

/* vmaf_ordsum_plan() on an fp32 prefix: the binade the chunk starts and ends
 * in, by the advice. */
VMAF_SYCL_ALWAYS_INLINE int advice_plan(float before, float after, float chunk_sum, int scale_log2)
{
    if (chunk_sum == 0.0f)
        return VMAF_ORDSUM_PLAN_ZERO;
    const int e = advice_binade(before, scale_log2);
    return e == advice_binade(after, scale_log2) ? e : VMAF_ORDSUM_PLAN_TERMS;
}

/* The lower of the two binades a kept chunk's runs are composed under: the
 * one below the binade the chunk ends in. A chunk that crosses one binade
 * starts there. A chunk that crosses several (the first chunks of a sum,
 * which starts at zero) spends most of its terms in the last two, because the
 * sum doubles from one binade to the next. */
VMAF_SYCL_ALWAYS_INLINE int expected_binade(float after, int scale_log2)
{
    const int end = advice_binade(after, scale_log2);
    return vmaf_ordsum_plan_is_binade(end) ? end - 1 : VMAF_ORDSUM_PLAN_TERMS;
}

/* Where one sum's plan goes. */
struct SumPlan {
    int16_t *plan;        /* [chunk] */
    int32_t *slot_chunk;  /* [slot]: the chunk kept there, or -1 */
    int16_t *slot_binade; /* [slot]: expected_binade() of the chunk */
};

/* The plan of every chunk of one sum, from the advice sums `sums[chunk *
 * stride]`. The first `slot_limit` chunks sent to the term-by-term path get a
 * slot each. */
VMAF_SYCL_ALWAYS_INLINE void plan_chunks(const float *sums, size_t stride, unsigned chunks,
                                         int scale_log2, const SumPlan &out, unsigned slot_limit)
{
    float prefix = 0.0f;
    unsigned slots = 0u;
    for (unsigned chunk = 0; chunk < chunks; chunk++) {
        const float chunk_sum = sums[(size_t)chunk * stride];
        const float after = prefix + chunk_sum;
        int value = advice_plan(prefix, after, chunk_sum, scale_log2);
        if (value == VMAF_ORDSUM_PLAN_TERMS && slots < slot_limit) {
            out.slot_chunk[slots] = (int32_t)chunk;
            out.slot_binade[slots] = (int16_t)expected_binade(after, scale_log2);
            value = kSlotCodeBase + (int)slots;
            slots++;
        }
        out.plan[chunk] = (int16_t)value;
        prefix = after;
    }
    for (unsigned slot = slots; slot < kSlots; slot++)
        out.slot_chunk[slot] = -1;
}

/* Appends a term to a run under two plans: `run` holds the run's increments
 * under `binade` and under the next binade, [binade, binade + 1][even, odd],
 * and takes the term's. A chunk without an expected binade keeps zeros, which
 * the walk does not read. */
VMAF_SYCL_ALWAYS_INLINE void stage_run(uint64_t bits, int binade, int64_t *run)
{
    if (!vmaf_ordsum_plan_is_binade(binade))
        return;
    const VmafOrdsumUnits here =
        vmaf_ordsum_then(vmaf_ordsum_units(run[0], run[1]), vmaf_ordsum_term_bits(bits, binade));
    const VmafOrdsumUnits next = vmaf_ordsum_then(vmaf_ordsum_units(run[2], run[3]),
                                                  vmaf_ordsum_term_bits(bits, binade + 1));
    run[0] = here.even;
    run[1] = here.odd;
    run[2] = next.even;
    run[3] = next.odd;
}

/* fl64(sum + term) on bit patterns, for non-negative values; a NaN stays.
 * While the sum stays in its binade the add is the term's integer increment
 * (ordered_sum.h); the add that leaves the binade, and one onto a zero sum,
 * is the fp64 addition in integers. */
VMAF_SYCL_ALWAYS_INLINE uint64_t add_bits(uint64_t sum, uint64_t term)
{
    if (vmaf_ordsum_is_nan_bits(sum) || vmaf_ordsum_is_nan_bits(term))
        return kNanBits;
    const int binade = vmaf_ordsum_binade_bits(sum);
    if (vmaf_ordsum_plan_is_binade(binade)) {
        uint64_t next = sum;
        if (vmaf_ordsum_add_chunk_bits(&next, binade, vmaf_ordsum_term_bits(term, binade)))
            return next;
    }
    return vmaf_sycl_soft::signed_bits(vmaf_sycl_soft::signed_add(
        vmaf_sycl_soft::signed_from_bits(sum), vmaf_sycl_soft::signed_from_bits(term)));
}

/* What the walk of one sum reads. */
struct SumWalk {
    const int16_t *plan;        /* [chunk] */
    const int64_t *units;       /* [chunk][even, odd] */
    const int16_t *slot_binade; /* [slot] */
    const uint64_t *terms;      /* [slot][term of the chunk] */
    const int64_t *run_units;   /* [slot][run][kRunUnits] */
    unsigned chunks;
    size_t count; /* terms in the sum */
};

/* A chunk with a slot: run after run. A run is added from its increment when
 * the sum is in the chunk's expected binade, or in the next one, and stays
 * there; otherwise its terms are added one by one. */
VMAF_SYCL_ALWAYS_INLINE uint64_t add_slot_chunk(const SumWalk &w, size_t slot, uint64_t sum)
{
    const int expected = (int)w.slot_binade[slot];
    const bool planned = vmaf_ordsum_plan_is_binade(expected);
    const uint64_t *terms = w.terms + slot * kChunk;
    const int64_t *runs = w.run_units + slot * kRuns * kRunUnits;
    for (unsigned run = 0; run < kRuns; run++) {
        const int binade = vmaf_ordsum_binade_bits(sum);
        const auto ahead = (unsigned)(binade - expected);
        bool added = false;
        if (planned && ahead < 2u) {
            const int64_t *units = runs + ((size_t)run * 2u + ahead) * 2u;
            added = vmaf_ordsum_add_chunk_bits(&sum, binade,
                                               vmaf_ordsum_units(units[0], units[1])) != 0;
        }
        for (unsigned i = 0; !added && i < kRun; i++)
            sum = add_bits(sum, terms[(size_t)run * kRun + i]);
    }
    return sum;
}

/* The sum of w.count terms in their order. `term_of(i)` is the fp64 bit
 * pattern of term i; the walk asks for it only in a chunk whose plan does not
 * hold and that has no slot. */
template <typename TermOf>
VMAF_SYCL_ALWAYS_INLINE uint64_t walk_sum(const SumWalk &w, TermOf term_of)
{
    uint64_t sum = 0u;
    for (unsigned chunk = 0; chunk < w.chunks; chunk++) {
        const int16_t entry = w.plan[chunk];
        const int64_t *units = w.units + (size_t)chunk * 2u;
        if (vmaf_ordsum_add_chunk_bits(&sum, plan_of(entry), vmaf_ordsum_units(units[0], units[1])))
            continue;
        const int kept = slot_of(entry);
        if (kept >= 0) {
            sum = add_slot_chunk(w, (size_t)kept, sum);
            continue;
        }
        const size_t first = (size_t)chunk * kChunk;
        const size_t end = first + kChunk < w.count ? first + kChunk : w.count;
        for (size_t i = first; i < end; i++)
            sum = add_bits(sum, term_of(i));
    }
    return sum;
}

} // namespace vmaf_sycl_ordsum

#endif /* VMAF_FEATURE_SYCL_SYCL_ORDERED_SUM_H_ */
