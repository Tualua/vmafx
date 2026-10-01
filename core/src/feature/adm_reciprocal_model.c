/**
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: EUPL-1.2
 *
 *  Host probe of the reciprocal estimate the float ADM decouple divides with
 *  (ADR-1420). See adm_reciprocal_model.h.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "adm_float_reference.h"
#include "adm_reciprocal_model.h"

#define ADM_PROBE_MANTISSAS (1u << 23)
/* Mantissa stride of the per-exponent sweep: a prime, so the sampled
 * mantissas do not line up with the table's buckets. */
#define ADM_PROBE_MANTISSA_STEP 4099u
#define ADM_PROBE_EXPONENTS 256u
#define ADM_PROBE_ONE_EXPONENT 127u

typedef uint32_t (*AdmReciprocalPredictor)(const uint32_t *table, uint32_t bits);

static uint32_t adm_bits_of(float x)
{
    uint32_t bits;
    memcpy(&bits, &x, sizeof(bits));
    return bits;
}

static float adm_float_of(uint32_t bits)
{
    float x;
    memcpy(&x, &bits, sizeof(x));
    return x;
}

static uint32_t adm_predict_ieee(const uint32_t *table, uint32_t bits)
{
    (void)table;
    return adm_bits_of(1.0f / adm_float_of(bits));
}

static bool adm_is_nan_bits(uint32_t bits)
{
    return (bits & 0x7fffffffu) > 0x7f800000u;
}

/* The host's estimate of one input against a predictor. Two NaNs agree
 * whatever their payloads: the decouple propagates a NaN, it never reads one. */
static bool adm_probe_input(AdmReciprocalPredictor predict, const uint32_t *table, uint32_t bits)
{
    const uint32_t host = adm_bits_of(adm_divs_reciprocal_estimate_s(adm_float_of(bits)));
    const uint32_t model = predict(table, bits);
    return host == model || (adm_is_nan_bits(host) && adm_is_nan_bits(model));
}

/* Every mantissa at one exponent, then every exponent (zeros, denormals,
 * infinities and NaNs included) and both signs at mantissas spread over the
 * range and at the last one. */
static bool adm_probe_predictor(AdmReciprocalPredictor predict, const uint32_t *table)
{
    bool agree = true;
    for (uint32_t mantissa = 0u; mantissa < ADM_PROBE_MANTISSAS; mantissa++)
        agree = adm_probe_input(predict, table, (ADM_PROBE_ONE_EXPONENT << 23) | mantissa) && agree;

    for (uint32_t i = 0u; i < 2u * ADM_PROBE_EXPONENTS; i++) {
        const uint32_t base = i << 23; /* sign and exponent */
        for (uint32_t mantissa = 0u; mantissa < ADM_PROBE_MANTISSAS;
             mantissa += ADM_PROBE_MANTISSA_STEP)
            agree = adm_probe_input(predict, table, base | mantissa) && agree;
        agree = adm_probe_input(predict, table, base | (ADM_PROBE_MANTISSAS - 1u)) && agree;
    }
    return agree;
}

void adm_reciprocal_model_probe(AdmReciprocalModel *model)
{
    assert(model);
    memset(model, 0, sizeof(*model));
    model->division = ADM_DIVISION_IEEE;
    model->reproduces_reference = true;
    if (!adm_divs_is_reciprocal_s())
        return;

    const unsigned bucket_shift = 23u - ADM_RECIPROCAL_INDEX_BITS;
    for (uint32_t i = 0u; i < ADM_RECIPROCAL_TABLE_SIZE; i++) {
        const uint32_t in = (ADM_PROBE_ONE_EXPONENT << 23) | (i << bucket_shift);
        model->table[i] = adm_bits_of(adm_divs_reciprocal_estimate_s(adm_float_of(in)));
    }
    if (adm_probe_predictor(adm_reciprocal_model_bits, model->table)) {
        model->division = ADM_DIVISION_RECIPROCAL_TABLE;
        return;
    }

    /* Not a table of the top mantissa bits. An emulator that computes the
     * estimate as the IEEE reciprocal is still reproduced exactly; anything
     * else is not. */
    model->division = ADM_DIVISION_RECIPROCAL_IEEE;
    model->reproduces_reference = adm_probe_predictor(adm_predict_ieee, model->table);
}
