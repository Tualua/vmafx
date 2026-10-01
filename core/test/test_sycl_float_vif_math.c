/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * ADR-1422: the arithmetic float_vif_sycl's kernels run, against the CPU
 * extractor's own routine, bit for bit, on the host and on the device.
 *
 * feature/sycl/sycl_float_vif_math.h holds vif_pixel_statistic_s() and
 * log2f_approx() for a device without an fp64 type (ADR-0220). The reference
 * evaluates two expressions in fp64, because `vif_sigma_nsq` is a double:
 *
 *     1.0f + (g * g * sigma1_sq) / (sv_sq + vif_sigma_nsq)
 *     1.0f + (sigma1_sq) / (vif_sigma_nsq)
 *
 * The header evaluates them as exact fp32 pairs and, where a pair lies next
 * to an fp32 rounding boundary, replays the reference's fp64 operations in
 * 64-bit integers. This test checks, through test_sycl_float_vif_math_probe.cpp:
 *
 *   - the statistic against vif_statistic_s() on a 1x1 plane, which returns
 *     exactly one pixel's numerator and denominator term, over inputs that
 *     reach every branch and several values of vif_sigma_nsq (zero included)
 *     and vif_enhn_gain_limit: on the host, and in a kernel on the default
 *     GPU;
 *   - the two fp64 expressions against the compiler's own fp64, three ways:
 *     as the kernels select, by the integer replay alone on every sample, and
 *     by the pair alone;
 *   - samples next to a rounding boundary, built so that the replay is taken,
 *     and twelve operands on which the pair alone rounds to the wrong value.
 *
 * A device log2, an fp32 vif_sigma_nsq or a wrong rounding in the integer
 * replay each fail at least one of these.
 *
 * Skip behaviour: the host checks always run; without a SYCL GPU the device
 * check is skipped and the test exits 77.
 */

#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test.h"

#include "feature/vif_tools.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. ADR-1138. */

void vmaf_test_sycl_fvif_host(const float *moments, size_t n, double sigma_nsq, double gain_limit,
                              float *num, float *den);
int vmaf_test_sycl_fvif_device(const float *moments, size_t n, double sigma_nsq, double gain_limit,
                               float *num, float *den);
int vmaf_test_sycl_fvif_ratio(float numerator, int has_addend, float addend, double sigma_nsq,
                              float out[3]);

enum {
    STAT_SAMPLES = 200000,
    RATIO_SAMPLES = 2000000,
    MOMENT_FLOATS = 5,
    SIGMA_NSQ_COUNT = 5,
    GAIN_LIMIT_COUNT = 3,
};

static const double SIGMA_NSQ[SIGMA_NSQ_COUNT] = {2.0, 1.5, 0.3, 5.0, 0.0};
static const double GAIN_LIMIT[GAIN_LIMIT_COUNT] = {100.0, 1.0, 1.7};

static float moments[STAT_SAMPLES * MOMENT_FLOATS];
static float cpu_num[STAT_SAMPLES];
static float cpu_den[STAT_SAMPLES];
static float twin_num[STAT_SAMPLES];
static float twin_den[STAT_SAMPLES];

/* Deterministic generator: the same inputs on every host. */
static uint32_t rng_state = 0x2545f491u;

static uint32_t rng_next(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}

/* Uniform in [0, 1). */
static float rng_unit(void)
{
    return (float)(rng_next() >> 8) * (1.0f / 16777216.0f);
}

static uint32_t float_bits(float value)
{
    uint32_t bits = 0u;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static int same_bits(float a, float b)
{
    return (a != a && b != b) || float_bits(a) == float_bits(b);
}

/* One pixel's five moments. `kind` selects the variance regime so that every
 * branch of vif_pixel_statistic_s() is reached: flat reference, flat
 * distortion, variances below and above sigma_nsq, negative covariance and
 * moments that make a variance negative before the clamp. */
static void random_moments(unsigned kind, float *m)
{
    static const float scale[] = {0.0f, 1.0e-11f, 1.0e-3f, 1.5f, 40.0f, 3000.0f, 16000.0f};
    const unsigned levels = (unsigned)(sizeof(scale) / sizeof(scale[0]));
    const float s1 = scale[kind % levels] * rng_unit();
    const float s2 = scale[(kind / levels) % levels] * rng_unit();
    const float rho = 2.0f * rng_unit() - 1.0f;
    m[0] = 255.0f * rng_unit() - 128.0f;
    m[1] = 255.0f * rng_unit() - 128.0f;
    m[2] = s1 + m[0] * m[0];
    m[3] = s2 + m[1] * m[1];
    m[4] = rho * sqrtf(s1 * s2) + m[0] * m[1];
    if (kind % 11u == 0u)
        m[2] = m[0] * m[0] - 1.0e-3f * rng_unit();
}

static void fill_moments(void)
{
    rng_state = 0x2545f491u;
    for (unsigned i = 0u; i < STAT_SAMPLES; i++)
        random_moments(i, &moments[(size_t)i * MOMENT_FLOATS]);
}

/* The CPU's terms of every sample: vif_statistic_s() on a 1x1 plane. */
static void cpu_statistic(double sigma_nsq, double gain_limit)
{
    for (unsigned i = 0u; i < STAT_SAMPLES; i++) {
        const float *m = &moments[(size_t)i * MOMENT_FLOATS];
        vif_statistic_s(&m[0], &m[1], &m[2], &m[3], &m[4], &cpu_num[i], &cpu_den[i], 1, 1,
                        (int)sizeof(float), (int)sizeof(float), (int)sizeof(float),
                        (int)sizeof(float), (int)sizeof(float), gain_limit, sigma_nsq);
    }
}

static unsigned count_differing(const char *where, double sigma_nsq, double gain_limit)
{
    unsigned differing = 0u;
    for (unsigned i = 0u; i < STAT_SAMPLES; i++) {
        if (same_bits(twin_num[i], cpu_num[i]) && same_bits(twin_den[i], cpu_den[i]))
            continue;
        if (differing < 5u) {
            (void)fprintf(stderr,
                          "\n%s sample %u (nsq=%g egl=%g): num %.9g vs cpu %.9g, den %.9g vs cpu "
                          "%.9g",
                          where, i, sigma_nsq, gain_limit, (double)twin_num[i], (double)cpu_num[i],
                          (double)twin_den[i], (double)cpu_den[i]);
        }
        differing++;
    }
    if (differing != 0u)
        (void)fprintf(stderr, "\n%s: %u of %d samples differ\n", where, differing, STAT_SAMPLES);
    return differing;
}

static char *test_host_statistic_is_vif_pixel_statistic_s(void)
{
    fill_moments();
    for (unsigned n = 0u; n < SIGMA_NSQ_COUNT; n++) {
        for (unsigned g = 0u; g < GAIN_LIMIT_COUNT; g++) {
            cpu_statistic(SIGMA_NSQ[n], GAIN_LIMIT[g]);
            vmaf_test_sycl_fvif_host(moments, STAT_SAMPLES, SIGMA_NSQ[n], GAIN_LIMIT[g], twin_num,
                                     twin_den);
            mu_assert("sycl_float_vif_math.h differs from vif_pixel_statistic_s() on the host",
                      count_differing("host", SIGMA_NSQ[n], GAIN_LIMIT[g]) == 0u);
        }
    }
    return NULL;
}

static char *test_device_statistic_is_vif_pixel_statistic_s(void)
{
    fill_moments();
    for (unsigned n = 0u; n < SIGMA_NSQ_COUNT; n++) {
        for (unsigned g = 0u; g < GAIN_LIMIT_COUNT; g++) {
            const int err = vmaf_test_sycl_fvif_device(moments, STAT_SAMPLES, SIGMA_NSQ[n],
                                                       GAIN_LIMIT[g], twin_num, twin_den);
            if (err == -ENODEV) {
                (void)fprintf(stderr, "[skip: no SYCL GPU] ");
                mu_skipped = 1;
                return NULL;
            }
            mu_assert("the device statistic kernel failed", err == 0);
            cpu_statistic(SIGMA_NSQ[n], GAIN_LIMIT[g]);
            mu_assert("sycl_float_vif_math.h differs from vif_pixel_statistic_s() on the device",
                      count_differing("device", SIGMA_NSQ[n], GAIN_LIMIT[g]) == 0u);
        }
    }
    return NULL;
}

/* What the reference computes: the quotient and the sum in fp64, one rounding
 * to fp32. */
static float reference_ratio(float numerator, int has_addend, float addend, double sigma_nsq)
{
    const double denominator = has_addend ? (double)addend + sigma_nsq : sigma_nsq;
    return (float)(1.0 + (double)numerator / denominator);
}

typedef struct RatioCounts {
    unsigned selected_wrong;
    unsigned replay_wrong;
    unsigned pair_wrong;
    unsigned replays;
} RatioCounts;

static void check_ratio(float numerator, int has_addend, float addend, double sigma_nsq,
                        RatioCounts *counts)
{
    float out[3] = {0.0f, 0.0f, 0.0f};
    counts->replays +=
        (unsigned)vmaf_test_sycl_fvif_ratio(numerator, has_addend, addend, sigma_nsq, out);
    const float reference = reference_ratio(numerator, has_addend, addend, sigma_nsq);
    if (!same_bits(out[0], reference) || !same_bits(out[2], reference)) {
        if (counts->selected_wrong + counts->replay_wrong < 5u) {
            (void)fprintf(stderr,
                          "\nnumerator=%a addend=%a (%d) nsq=%.17g: fp64 %a, selected %a, "
                          "replay %a",
                          (double)numerator, (double)addend, has_addend, sigma_nsq,
                          (double)reference, (double)out[0], (double)out[2]);
        }
    }
    counts->selected_wrong += same_bits(out[0], reference) ? 0u : 1u;
    counts->replay_wrong += same_bits(out[2], reference) ? 0u : 1u;
    counts->pair_wrong += same_bits(out[1], reference) ? 0u : 1u;
}

/* Random operands over the ranges the statistic produces: products of a gain
 * below 100 and a variance below 2^14, residual variances from 1e-10 up. */
static char *test_ratio_is_the_fp64_expression(void)
{
    static const float scale[] = {1.0e-9f, 1.0e-3f, 1.5f, 40.0f, 3000.0f, 16000.0f};
    RatioCounts counts = {0u, 0u, 0u, 0u};
    rng_state = 0x9e3779b9u;
    for (unsigned i = 0u; i < RATIO_SAMPLES; i++) {
        const double sigma_nsq = SIGMA_NSQ[i % 4u];
        const float variance = scale[(i / 4u) % 6u] * rng_unit() + 1.0e-30f;
        const float residual = scale[(i / 24u) % 6u] * rng_unit() + 1.0e-10f;
        const float gain = (i & 64u) ? 3.0f * rng_unit() : 100.0f * rng_unit();
        const float product = gain * gain * variance + 1.0e-30f;
        check_ratio(product, 1, residual, sigma_nsq, &counts);
        check_ratio(variance, 0, 0.0f, sigma_nsq, &counts);
    }
    mu_assert("the integer replay is not the reference's fp64 arithmetic",
              counts.replay_wrong == 0u);
    mu_assert("the selected value is not the reference's fp64 arithmetic",
              counts.selected_wrong == 0u);
    return NULL;
}

/* Operands built to land next to a rounding boundary of the sum, where the
 * kernels take the integer replay: the quotient is (k + 1/2) fp32 steps of a
 * sum in [1, 2), for small k, so that the numerator's own rounding leaves it
 * within a few thousandths of a step of the boundary. The test also requires
 * that most of these samples do take the replay, so it cannot pass by never
 * reaching it. */
static char *test_ratio_next_to_a_rounding_boundary(void)
{
    enum { BOUNDARY_SAMPLES = 400000 };
    RatioCounts counts = {0u, 0u, 0u, 0u};
    unsigned checked = 0u;
    rng_state = 0x1b873593u;
    for (unsigned i = 0u; i < BOUNDARY_SAMPLES; i++) {
        const double sigma_nsq = SIGMA_NSQ[i % 4u];
        const int has_addend = (int)((i / 4u) % 2u);
        const float addend = 40.0f * rng_unit() + 1.0e-10f;
        const double denominator = has_addend ? (double)addend + sigma_nsq : sigma_nsq;
        const double steps = (double)(rng_next() % 1024u);
        const float numerator = (float)(denominator * ((steps + 0.5) * 0x1p-23));
        check_ratio(numerator, has_addend, addend, sigma_nsq, &counts);
        checked++;
    }
    mu_assert("next to a rounding boundary the integer replay is not the reference's fp64",
              counts.replay_wrong == 0u);
    mu_assert("next to a rounding boundary the selected value is not the reference's fp64",
              counts.selected_wrong == 0u);
    if (counts.replays < checked / 2u) {
        (void)fprintf(stderr, "\nonly %u of %u boundary samples take the replay\n", counts.replays,
                      checked);
    }
    mu_assert("the boundary samples do not reach the integer replay",
              counts.replays >= checked / 2u);
    (void)fprintf(stderr, "[replay taken on %u of %u, pair alone wrong on %u] ", counts.replays,
                  checked, counts.pair_wrong);
    return NULL;
}

/* Operands on which the pair alone rounds to the wrong fp32 value: found by a
 * search over 8.4e9 random quotients (85 such operands, about one in 1e8;
 * none where the selected value was wrong). Most are exact ties, where the
 * reference's two roundings and the pair's one disagree. The kernels must
 * take the replay on each, and the test fails if the selection is removed. */
typedef struct RatioWitness {
    float numerator;
    int has_addend;
    float addend;
    double sigma_nsq;
} RatioWitness;

static char *test_pair_alone_is_not_the_fp64_expression(void)
{
    static const RatioWitness witness[] = {
        {0x1.2b38p-11f, 1, 0x1.543d0cp-33f, 5.0},   {0x1.800aa2p-22f, 1, 0x1.c5b018p-13f, 2.0},
        {0x1.4f1662p-10f, 1, 0x1.234676p-13f, 5.0}, {0x1.18p-18f, 1, 0x1.febc18p-31f, 2.0},
        {0x1.416ce8p+20f, 1, 0x1.11354ap+3f, 0.3},  {0x1.2f3788p+2f, 1, 0x1.374cfp+0f, 4.7},
        {0x1.653388p+5f, 1, 0x1.3bd38p-11f, 4.7},   {0x1.333334p-26f, 0, 0.0f, 0.3},
        {0x1.241e6ep+1f, 1, 0x1.68cc86p-12f, 1.5},  {0x1.69570ep+26f, 1, 0x1.e4383cp+3f, 4.7},
        {0x1.3949p+17f, 1, 0x1.55ae86p+0f, 1.5},    {0x1.f2p-18f, 1, 0x1.069bb8p-31f, 1.5},
    };
    const unsigned count = (unsigned)(sizeof(witness) / sizeof(witness[0]));
    RatioCounts counts = {0u, 0u, 0u, 0u};
    for (unsigned i = 0u; i < count; i++) {
        check_ratio(witness[i].numerator, witness[i].has_addend, witness[i].addend,
                    witness[i].sigma_nsq, &counts);
    }
    mu_assert("the selected value is not the reference's fp64 on a witness",
              counts.selected_wrong == 0u && counts.replay_wrong == 0u);
    mu_assert("every witness must take the integer replay", counts.replays == count);
    mu_assert("the witnesses no longer show the pair alone rounding differently",
              counts.pair_wrong == count);
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_host_statistic_is_vif_pixel_statistic_s);
    mu_run_test(test_ratio_is_the_fp64_expression);
    mu_run_test(test_ratio_next_to_a_rounding_boundary);
    mu_run_test(test_pair_alone_is_not_the_fp64_expression);
    mu_run_test(test_device_statistic_is_vif_pixel_statistic_s);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
