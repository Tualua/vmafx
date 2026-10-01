/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * ADR-1432: the gain terms vif_sycl's kernels compute, against the
 * reference's fp64 expressions, on the host and on the device.
 *
 * integer_vif.c::vif_accumulate_pixel() (and the same lines in
 * x86/vif_avx2.c and x86/vif_avx512.c) forms a pixel's gain in fp64 and
 * truncates two results to integers before the log2 table:
 *
 *     const double eps = 65536 * 1.0e-10;
 *     double g = sigma12 / (sigma1_sq + eps);
 *     int32_t sv_sq = sigma2_sq - g * sigma12;
 *     sv_sq = (uint32_t)(MAX(sv_sq, 0));
 *     g = MIN(g, vif_enhn_gain_limit);
 *     ... (int64_t)((g * g * sigma1_sq)) ...
 *
 * feature/sycl/sycl_integer_vif_math.h returns the same two integers without
 * an fp64 type (ADR-0220): one integer division decides both, and a sample
 * whose exact value lies within the fp64 chain's rounding error of an integer
 * replays the reference's fp64 operations in 64-bit integers. This test
 * checks, through test_sycl_integer_vif_math_probe.cpp, with reference_terms()
 * below holding the reference's lines verbatim:
 *
 *   - the replay alone on every sample (it must be the reference on all of
 *     them, since it is what an undecided sample gets);
 *   - the integer evaluation alone on every sample it claims to decide;
 *   - the selected value, on the host and in a kernel on the default GPU;
 *   - over random variances and over the cases that sit on a boundary:
 *     identical planes (sigma12 = sigma1_sq = sigma2_sq), quotients that are
 *     integers or just beside one, a gain at the limit, powers of two, and
 *     gain limits that are integers (100, 1) and that are not (1.2).
 *
 * test_sycl_vif_exact_gain_contract.py pins that integer_vif.c still holds
 * the lines reference_terms() copies.
 *
 * Skip behaviour: the host checks always run; without a SYCL GPU the device
 * check is skipped and the test exits 77.
 */

#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "test.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. ADR-1138. */

void vmaf_test_sycl_ivif_host(const uint32_t *sigmas, size_t n, double gain_limit, int path,
                              uint32_t *sv_sq, int64_t *gg_sigma, size_t *replays);
int vmaf_test_sycl_ivif_device(const uint32_t *sigmas, size_t n, double gain_limit, uint32_t *sv_sq,
                               int64_t *gg_sigma);

enum {
    SAMPLES = 400000,
    LIMIT_COUNT = 4,
    PATH_SELECTED = 0,
    PATH_INTEGER = 1,
    PATH_REPLAY = 2,
};

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

static const double GAIN_LIMIT[LIMIT_COUNT] = {100.0, 1.0, 1.2, 37.0};

static uint32_t sigmas[SAMPLES * 3];
static uint32_t ref_sv[SAMPLES];
static int64_t ref_gg[SAMPLES];
static uint32_t got_sv[SAMPLES];
static int64_t got_gg[SAMPLES];
static uint32_t fast_sv[SAMPLES];
static int64_t fast_gg[SAMPLES];

/* integer_vif.c::vif_accumulate_pixel(), the lines between the two integer
 * accumulations, verbatim. */
static void reference_terms(int32_t sigma1_sq, int32_t sigma2_sq, int32_t sigma12,
                            double vif_enhn_gain_limit, uint32_t *sv_out, int64_t *gg_out)
{
    const double eps = 65536 * 1.0e-10;
    double g = sigma12 / (sigma1_sq + eps); // this epsilon can go away
    int32_t sv_sq = sigma2_sq - g * sigma12;

    sv_sq = (uint32_t)(MAX(sv_sq, 0));

    g = MIN(g, vif_enhn_gain_limit);

    *sv_out = (uint32_t)sv_sq;
    *gg_out = (int64_t)((g * g * sigma1_sq));
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

static uint32_t clamp_variance(uint64_t value)
{
    if (value == 0u)
        return 1u;
    return value >= 0x80000000ULL ? 0x7fffffffu : (uint32_t)value;
}

/* A value in [1, 2^31) of a random magnitude; `near_power` puts it beside a
 * power of two. */
static uint32_t random_variance(int near_power)
{
    const unsigned bits = 1u + (unsigned)(rng_next() % 31u);
    uint64_t value = rng_next() & ((1ULL << bits) - 1u);
    if (near_power)
        value = (1ULL << (bits - 1u)) + (rng_next() % 3u) - 1u;
    return clamp_variance(value);
}

/* One sample; `kind` cycles through the boundary cases of the header. */
static void fill_sample(unsigned kind, double limit, uint32_t *s)
{
    uint32_t s1 = random_variance((int)(rng_next() & 1u));
    if (s1 < 131072u)
        s1 += 131072u;
    uint32_t s2 = random_variance((int)(rng_next() & 1u));
    uint32_t s12 = random_variance((int)(rng_next() & 1u));
    switch (kind % 8u) {
    case 0: /* identical planes */
        s12 = s1;
        s2 = s1;
        break;
    case 1: /* sigma2_sq - g * sigma12 beside 0, 1 or 2 */
        s12 = clamp_variance(((uint64_t)s1 * (rng_next() % 1000u)) / 1000u + 1u);
        s2 = clamp_variance(((uint64_t)s12 * s12) / s1 + (rng_next() % 3u));
        break;
    case 2: /* sigma12^2 / sigma1_sq an integer or beside one */
        s12 = clamp_variance(((uint64_t)s1 * ((rng_next() % 4096u) + 1u)) >> 12);
        break;
    case 3: /* the gain at the limit */
        s12 = clamp_variance((uint64_t)llround(limit * (double)s1) + (rng_next() % 3u) - 1u);
        break;
    default: /* random */
        break;
    }
    s[0] = s1;
    s[1] = s2;
    s[2] = s12;
}

static void fill_samples(double limit)
{
    rng_state = 0x9e3779b97f4a7c15ULL;
    for (unsigned i = 0u; i < SAMPLES; i++) {
        uint32_t *s = &sigmas[(size_t)i * 3u];
        fill_sample(i, limit, s);
        reference_terms((int32_t)s[0], (int32_t)s[1], (int32_t)s[2], limit, &ref_sv[i], &ref_gg[i]);
    }
}

static unsigned count_differing(const char *where, double limit, const uint32_t *sv,
                                const int64_t *gg)
{
    unsigned differing = 0u;
    for (unsigned i = 0u; i < SAMPLES; i++) {
        if (sv[i] == ref_sv[i] && gg[i] == ref_gg[i])
            continue;
        if (differing < 5u) {
            const uint32_t *s = &sigmas[(size_t)i * 3u];
            (void)fprintf(stderr,
                          "\n%s sample %u (limit %g) sigma1_sq=%u sigma2_sq=%u sigma12=%u: sv_sq "
                          "%u vs %u, gg %lld vs %lld",
                          where, i, limit, s[0], s[1], s[2], sv[i], ref_sv[i], (long long)gg[i],
                          (long long)ref_gg[i]);
        }
        differing++;
    }
    if (differing != 0u)
        (void)fprintf(stderr, "\n%s: %u of %d samples differ\n", where, differing, SAMPLES);
    return differing;
}

static char *test_replay_is_the_fp64_arithmetic(void)
{
    for (unsigned l = 0u; l < LIMIT_COUNT; l++) {
        fill_samples(GAIN_LIMIT[l]);
        vmaf_test_sycl_ivif_host(sigmas, SAMPLES, GAIN_LIMIT[l], PATH_REPLAY, got_sv, got_gg, NULL);
        mu_assert("the integer replay is not the reference's fp64 arithmetic",
                  count_differing("replay", GAIN_LIMIT[l], got_sv, got_gg) == 0u);
    }
    return NULL;
}

/* The integer evaluation is right wherever it claims to decide, and it hands
 * the boundary cases over: both halves of the selection are exercised. */
static char *test_integer_evaluation_decides_or_hands_over(void)
{
    for (unsigned l = 0u; l < LIMIT_COUNT; l++) {
        size_t replays = 0u;
        fill_samples(GAIN_LIMIT[l]);
        vmaf_test_sycl_ivif_host(sigmas, SAMPLES, GAIN_LIMIT[l], PATH_INTEGER, fast_sv, fast_gg,
                                 &replays);
        vmaf_test_sycl_ivif_host(sigmas, SAMPLES, GAIN_LIMIT[l], PATH_SELECTED, got_sv, got_gg,
                                 NULL);
        unsigned decided_wrong = 0u;
        unsigned handed_over_and_wrong = 0u;
        for (unsigned i = 0u; i < SAMPLES; i++) {
            const int fast_right = fast_sv[i] == ref_sv[i] && fast_gg[i] == ref_gg[i];
            const int selected_is_fast = got_sv[i] == fast_sv[i] && got_gg[i] == fast_gg[i];
            if (!fast_right && selected_is_fast)
                decided_wrong++;
            if (!fast_right && !selected_is_fast)
                handed_over_and_wrong++;
        }
        mu_assert("the selected value is not the reference's fp64 arithmetic",
                  count_differing("selected", GAIN_LIMIT[l], got_sv, got_gg) == 0u);
        mu_assert("the integer evaluation decided a sample wrongly", decided_wrong == 0u);
        mu_assert("no sample takes the replay: the boundary cases are not reached",
                  replays > 0u && replays < (size_t)SAMPLES);
        mu_assert("no boundary sample shows the integer evaluation alone being wrong",
                  handed_over_and_wrong > 0u);
    }
    return NULL;
}

static char *test_device_terms_are_the_fp64_arithmetic(void)
{
    for (unsigned l = 0u; l < LIMIT_COUNT; l++) {
        fill_samples(GAIN_LIMIT[l]);
        const int err = vmaf_test_sycl_ivif_device(sigmas, SAMPLES, GAIN_LIMIT[l], got_sv, got_gg);
        if (err == -ENODEV) {
            (void)fprintf(stderr, "[skip: no SYCL GPU] ");
            mu_skipped = 1;
            return NULL;
        }
        mu_assert("the device gain kernel failed", err == 0);
        mu_assert("the gain terms differ from the reference's fp64 arithmetic on the device",
                  count_differing("device", GAIN_LIMIT[l], got_sv, got_gg) == 0u);
    }
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_replay_is_the_fp64_arithmetic);
    mu_run_test(test_integer_evaluation_decides_or_hands_over);
    mu_run_test(test_device_terms_are_the_fp64_arithmetic);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
