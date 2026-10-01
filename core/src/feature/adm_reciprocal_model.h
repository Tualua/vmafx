/**
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: EUPL-1.2
 *
 *  A model of the reciprocal estimate the float ADM decouple divides with,
 *  for a device that has no such instruction (ADR-1420).
 *
 *  On x86 the reference computes t / o as t * rcp(o), where rcp() refines the
 *  processor's RCPSS estimate with one Newton step (adm_tools.c). RCPSS is
 *  specified by an error bound only, so two processors return different
 *  estimates for the same input and the reference's scores are those of the
 *  processor it runs on. A GPU twin reproduces them by evaluating the host's
 *  estimate itself:
 *
 *   - adm_reciprocal_model_bits() is the model. The estimate of a normal x is
 *     a function of the top ADM_RECIPROCAL_INDEX_BITS mantissa bits, scaled by
 *     the exponent; zeros and denormals give an infinity, infinities and
 *     results below the normal range give a zero, always with the sign of x.
 *     It works on bit patterns in integer arithmetic only, so every device
 *     compiler builds it unchanged; a device TU defines ADM_RECIPROCAL_FN
 *     before including this header.
 *   - adm_reciprocal_model_probe() builds the model's table from the host's
 *     own instruction and proves the model against it: every mantissa at one
 *     exponent, every exponent, both signs and the special values. A host the
 *     model does not describe gets a different division mode, never a table
 *     that is nearly right.
 */

#ifndef VMAF_SRC_FEATURE_ADM_RECIPROCAL_MODEL_H_
#define VMAF_SRC_FEATURE_ADM_RECIPROCAL_MODEL_H_

#include <stdint.h>

#define ADM_RECIPROCAL_INDEX_BITS 12
#define ADM_RECIPROCAL_TABLE_SIZE (1u << ADM_RECIPROCAL_INDEX_BITS)

/* How a twin divides in the decouple. */
#define ADM_DIVISION_IEEE 0u             /* n / d: the reference build does not use a reciprocal */
#define ADM_DIVISION_RECIPROCAL_TABLE 1u /* n * rcp(d), estimate from the probed table */
#define ADM_DIVISION_RECIPROCAL_IEEE 2u  /* n * rcp(d), estimate 1 / d */

#ifndef ADM_RECIPROCAL_FN
#define ADM_RECIPROCAL_FN static inline
#endif

/* Bit pattern of the estimate of the float whose bit pattern is `bits`.
 * `table[i]` is the bit pattern of the estimate of the float in [1, 2) whose
 * top ADM_RECIPROCAL_INDEX_BITS mantissa bits are `i`. */
ADM_RECIPROCAL_FN uint32_t adm_reciprocal_model_bits(const uint32_t *table, uint32_t bits)
{
    const uint32_t sign = bits & 0x80000000u;
    const uint32_t exponent = (bits >> 23) & 0xffu;
    const uint32_t mantissa = bits & 0x007fffffu;
    if (exponent == 0xffu)
        return (mantissa != 0u) ? (bits | 0x00400000u) : sign;
    if (exponent == 0u)
        return sign | 0x7f800000u;

    const uint32_t entry = table[mantissa >> (23 - ADM_RECIPROCAL_INDEX_BITS)];
    const int32_t scaled = (int32_t)(entry >> 23) + 127 - (int32_t)exponent;
    if (scaled < 1)
        return sign;
    return sign | ((uint32_t)scaled << 23) | (entry & 0x007fffffu);
}

#ifndef ADM_RECIPROCAL_DEVICE

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AdmReciprocalModel {
    /* ADM_DIVISION_* */
    uint32_t division;
    /* False when the host's estimate is neither the table model nor the IEEE
     * reciprocal: the twin then divides with ADM_DIVISION_RECIPROCAL_IEEE and
     * is close to the reference, not equal to it. */
    bool reproduces_reference;
    /* Filled for ADM_DIVISION_RECIPROCAL_TABLE. */
    uint32_t table[ADM_RECIPROCAL_TABLE_SIZE];
} AdmReciprocalModel;

/* Describe the division of this build of the reference on this host. */
void adm_reciprocal_model_probe(AdmReciprocalModel *model);

#ifdef __cplusplus
}
#endif

#endif /* ADM_RECIPROCAL_DEVICE */

#endif /* VMAF_SRC_FEATURE_ADM_RECIPROCAL_MODEL_H_ */
