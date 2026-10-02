/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * ADR-1446: the per-pixel terms ssimulacra2_sycl's kernels compute, against
 * the reference's fp64 expressions, on the host and on the device.
 *
 * ssimulacra2.c::ssim_map() and ::edge_diff_map() form six terms per sample
 * and channel in fp64 and add each into a sum of its own:
 *
 *     double d = 1.0 - ((double)num_m * (double)num_s / (double)denom_s);
 *     if (d < 0.0)
 *         d = 0.0;
 *     sum_l1 += d;
 *     sum_l4 += quartic(d);
 *
 *     double ed1 = fabs((double)r1[i] - (double)rm1[i]);
 *     double ed2 = fabs((double)r2[i] - (double)rm2[i]);
 *     double d1 = (1.0 + ed2) / (1.0 + ed1) - 1.0;
 *     vmaf_ss2_split_edge_difference(d1, &art, &det);
 *     s0 += art;
 *     s1 += quartic(art);
 *     s2 += det;
 *     s3 += quartic(det);
 *
 * feature/sycl/sycl_ssimulacra2_math.h returns those six doubles as bit
 * patterns without an fp64 type (ADR-0220): every fp64 operation is done on a
 * significand and an exponent in 64-bit integers
 * (feature/sycl/sycl_soft_signed.h). The sums the terms go into are ordered
 * sums of doubles (ADR-1433), so a term has to be the reference's double and
 * not a value near it. This test checks, through
 * test_sycl_ssimulacra2_math_probe.cpp, with reference_terms() below holding
 * the reference's lines verbatim and vmaf_ss2_split_edge_difference() being
 * the reference's own function:
 *
 *   - random inputs of the planes' range, inputs across 60 binades, an
 *     identical pair (d and d1 exactly zero), a ratio one fp32 step from 1,
 *     a negative d (clamped), operands of an edge difference that cancel, and
 *     differences far below 1 (1.0 + ed rounds);
 *   - a zero denominator of either sign, where the reference's quotient is
 *     an infinity and d is zero (clamped) or infinite, and the largest
 *     finite inputs, whose fourth power overflows: a term the reference
 *     computes as an infinity or a NaN has to come back as not finite, and
 *     every other one as the reference's bits;
 *   - that a positive and a negative d1, an exactly zero d and a non-finite
 *     term occur;
 *   - the same in a kernel on the default GPU, where the quotient's digits
 *     come from the device's fp32 division.
 *
 * test_sycl_ssimulacra2_exact_contract.py pins that ssimulacra2.c still
 * holds the lines reference_terms() copies.
 *
 * Skip behaviour: the host check always runs; without a SYCL GPU the device
 * check is skipped and the test exits 77.
 */

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test.h"

#include "feature/ssimulacra2_score.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. ADR-1138. */

void vmaf_test_sycl_ss2_host(const float *in, size_t n, uint64_t *out);
int vmaf_test_sycl_ss2_device(const float *in, size_t n, uint64_t *out);

enum {
    SAMPLES = 600000,
    INPUTS = 7,
    OUTPUTS = 6,
    SAMPLE_KINDS = 10,
};

static const char *const OUTPUT_NAME[OUTPUTS] = {"d",   "quartic(d)",  "art", "quartic(art)",
                                                 "det", "quartic(det)"};

static float inputs[(size_t)SAMPLES * INPUTS];
static uint64_t reference[(size_t)SAMPLES * OUTPUTS];
static uint64_t got[(size_t)SAMPLES * OUTPUTS];

/* ssimulacra2.c's quartic(). */
static inline double quartic(double x)
{
    x *= x;
    return x * x;
}

static uint64_t bits_of(double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static double value_of(uint64_t bits)
{
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

/* The reference's lines for one sample, verbatim; `in` is num_m, num_s,
 * denom_s, r1, rm1, r2, rm2 and `out` takes d, quartic(d), art, quartic(art),
 * det, quartic(det). */
static void reference_terms(const float *in, uint64_t *out)
{
    const float num_m = in[0];
    const float num_s = in[1];
    const float denom_s = in[2];
    double d = 1.0 - ((double)num_m * (double)num_s / (double)denom_s);
    if (d < 0.0)
        d = 0.0;
    const float *r1 = &in[3];
    const float *rm1 = &in[4];
    const float *r2 = &in[5];
    const float *rm2 = &in[6];
    const size_t i = 0u;
    double ed1 = fabs((double)r1[i] - (double)rm1[i]);
    double ed2 = fabs((double)r2[i] - (double)rm2[i]);
    double d1 = (1.0 + ed2) / (1.0 + ed1) - 1.0;
    double art;
    double det;
    vmaf_ss2_split_edge_difference(d1, &art, &det);
    out[0] = bits_of(d);
    out[1] = bits_of(quartic(d));
    out[2] = bits_of(art);
    out[3] = bits_of(quartic(art));
    out[4] = bits_of(det);
    out[5] = bits_of(quartic(det));
}

/* Deterministic generator: the same inputs on every host. */
static uint64_t rng_state = 0x9e3779b97f4a7c15ULL;

static uint64_t rng_next(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

/* A value in [0, 1). */
static float unit_random(void)
{
    return (float)((double)(rng_next() >> 11) / 9007199254740992.0);
}

/* A value of a random sign across 60 binades below 1. */
static float wide_random(void)
{
    const float magnitude = ldexpf(1.0f + unit_random(), -1 - (int)(rng_next() % 60u));
    return (rng_next() & 1u) ? -magnitude : magnitude;
}

/* One sample; `kind` cycles through the cases of the header. */
static void fill_sample(unsigned kind, float *in)
{
    in[0] = 1.0f - unit_random() * unit_random();
    in[2] = 0.0009f + unit_random() * 1e-3f;
    in[1] = in[2] * (1.0f - unit_random() * 0.1f);
    for (int f = 3; f < INPUTS; f++)
        in[f] = unit_random();
    switch (kind % SAMPLE_KINDS) {
    case 0: /* identical windows and pixels: d and d1 are exactly zero */
        in[0] = 1.0f;
        in[1] = in[2];
        in[5] = in[3];
        in[6] = in[4];
        break;
    case 1: /* the ratio one fp32 step from one */
        in[0] = 1.0f;
        in[1] = nextafterf(in[2], (rng_next() & 1u) ? 0.0f : 1.0f);
        break;
    case 2: /* d below zero: clamped */
        in[1] = in[2] * (1.0f + unit_random());
        break;
    case 3: /* a negative covariance: d above one */
        in[1] = -in[1];
        break;
    case 4: /* edge operands that cancel to a few units in the last place */
        in[4] = nextafterf(in[3], 2.0f);
        in[6] = nextafterf(in[5], -1.0f);
        break;
    case 5: /* every input across 60 binades */
        for (int f = 3; f < INPUTS; f++)
            in[f] = wide_random();
        in[0] = wide_random();
        in[1] = wide_random();
        break;
    case 6: /* a zero denominator of either sign, a product of either sign */
        in[2] = (rng_next() & 1u) ? -0.0f : 0.0f;
        in[1] = (rng_next() & 1u) ? -in[1] : in[1];
        break;
    case 7: /* terms beyond the fp64 range of their fourth power */
        in[0] = -FLT_MAX * unit_random();
        in[1] = FLT_MAX * unit_random();
        in[2] = FLT_MIN * (1.0f + unit_random());
        in[5] = FLT_MAX * unit_random();
        in[6] = -FLT_MAX * unit_random();
        in[3] = in[4];
        break;
    default: /* random */
        break;
    }
}

/* What the samples cover. */
typedef struct Coverage {
    unsigned zero_d;
    unsigned artifacts;
    unsigned details;
    unsigned not_finite;
} Coverage;

static Coverage fill_samples(void)
{
    Coverage coverage = {0u, 0u, 0u, 0u};
    rng_state = 0x9e3779b97f4a7c15ULL;
    for (unsigned i = 0u; i < SAMPLES; i++) {
        float *in = &inputs[(size_t)i * INPUTS];
        uint64_t *out = &reference[(size_t)i * OUTPUTS];
        fill_sample(i, in);
        reference_terms(in, out);
        coverage.zero_d += value_of(out[0]) == 0.0 ? 1u : 0u;
        coverage.artifacts += value_of(out[2]) > 0.0 ? 1u : 0u;
        coverage.details += value_of(out[4]) > 0.0 ? 1u : 0u;
        coverage.not_finite += isfinite(value_of(out[1])) ? 0u : 1u;
    }
    return coverage;
}

/* Equal bits; or two zeros (a zero has no sign in sycl_soft_signed.h, and a
 * zero of either sign adds nothing to a sum); or a term the reference
 * computes as an infinity or a NaN and the header returns as a NaN (either
 * makes the sum not finite, and the frame guard rejects the frame). */
static int same_double(uint64_t got_bits, uint64_t reference_bits)
{
    const double a = value_of(got_bits);
    const double b = value_of(reference_bits);
    if (!isfinite(b))
        return isnan(a);
    return got_bits == reference_bits || (a == 0.0 && b == 0.0);
}

static unsigned count_wrong(const char *where)
{
    unsigned wrong = 0u;
    for (size_t i = 0u; i < (size_t)SAMPLES * OUTPUTS; i++) {
        if (same_double(got[i], reference[i]))
            continue;
        if (wrong < 5u) {
            const float *in = &inputs[(i / OUTPUTS) * INPUTS];
            (void)fprintf(stderr,
                          "\n%s sample %zu %s: %a, reference %a (num_m %a num_s %a denom_s %a "
                          "r1 %a m1 %a r2 %a m2 %a)",
                          where, i / OUTPUTS, OUTPUT_NAME[i % OUTPUTS], value_of(got[i]),
                          value_of(reference[i]), in[0], in[1], in[2], in[3], in[4], in[5], in[6]);
        }
        wrong++;
    }
    if (wrong != 0u)
        (void)fprintf(stderr, "\n%s: %u of %d terms differ\n", where, wrong, SAMPLES * OUTPUTS);
    return wrong;
}

static char *test_terms_are_the_fp64_expressions(void)
{
    const Coverage coverage = fill_samples();
    vmaf_test_sycl_ss2_host(inputs, SAMPLES, got);
    mu_assert("a term is not the reference's fp64 expression", count_wrong("host") == 0u);
    mu_assert("no sample has d exactly zero", coverage.zero_d > 0u);
    mu_assert("no sample has a positive edge difference", coverage.artifacts > 0u);
    mu_assert("no sample has a negative edge difference", coverage.details > 0u);
    mu_assert("no sample has a term that is not finite", coverage.not_finite > 0u);
    return NULL;
}

static char *test_device_terms_are_the_fp64_expressions(void)
{
    (void)fill_samples();
    const int err = vmaf_test_sycl_ss2_device(inputs, SAMPLES, got);
    if (err == -ENODEV) {
        (void)fprintf(stderr, "[skip: no SYCL GPU] ");
        mu_skipped = 1;
        return NULL;
    }
    mu_assert("the device term kernel failed", err == 0);
    mu_assert("a term differs from the reference's fp64 expression on the device",
              count_wrong("device") == 0u);
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_terms_are_the_fp64_expressions);
    mu_run_test(test_device_terms_are_the_fp64_expressions);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
