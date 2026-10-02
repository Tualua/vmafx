/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * ADR-1446: the ordered sum a SYCL twin forms without an fp64 type
 * (feature/sycl/sycl_ordered_sum.h on feature/ordered_sum.h) against the loop
 * it stands for,
 *
 *     for (i = 0; i < n; i++)
 *         s += x[i];
 *
 * on the host and with the walk in a kernel on the default GPU.
 *
 * The header's claim is that every plan is advice: the walk adds a chunk or a
 * run from its integer increment only where that is the loop's result, and
 * adds the terms one by one everywhere else, so a wrong plan costs time and
 * never changes the sum. The test therefore gives the plan honest advice and
 * every kind of wrong advice and requires the loop's bits each time:
 *
 *   - advice sums that are right, half, double, a thousandth and a thousand
 *     times the chunk sums (plans one or several binades off);
 *   - "every term is zero" for chunks that are not, a constant, a NaN;
 *   - no slot at all and three slots, so that chunks which need their terms
 *     ask the walk for them; and honest advice with all slots, where the walk
 *     must ask for none;
 *   - terms of one magnitude, fourth powers down to 1e-60 with the scaled
 *     advice the extractor uses for them, mostly zeros, magnitudes that grow
 *     through 300 binades (more crossings than there are slots), a NaN in
 *     the middle, and lengths of zero, one, one chunk less one, and several
 *     chunks plus a rest.
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

#include "sycl_ordered_sum_probe.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. ADR-1138. */

enum {
    MAX_TERMS = 200000,
    KIND_COUNT = 6,
    LENGTH_COUNT = 6,
    PLAN_COUNT = 10,
    ALL_SLOTS = 128,
    FOURTH_SCALE_LOG2 = 88,
};

/* How the terms of a case are made. */
enum {
    KIND_UNIFORM = 0, /* around 1e-3 */
    KIND_FOURTH = 1,  /* fourth powers of those, 1e-12 and below */
    KIND_SPARSE = 2,  /* three in four are zero */
    KIND_GROWING = 3, /* each binade for a few hundred terms, 300 binades */
    KIND_TINY = 4,    /* fourth powers of 1e-15 */
    KIND_NAN = 5,     /* uniform with a NaN in the middle */
};

static const char *const KIND_NAME[KIND_COUNT] = {"uniform", "fourth", "sparse",
                                                  "growing", "tiny",   "nan"};
static const size_t LENGTH[LENGTH_COUNT] = {0u, 1u, 511u, 1000u, 60000u, MAX_TERMS};

/* A plan the walk is given. */
typedef struct Plan {
    const char *what;
    int advice;
    float factor;
    unsigned slot_limit;
} Plan;

static const Plan PLAN[PLAN_COUNT] = {
    {"honest", VMAF_TEST_ORDSUM_ADVICE_SUMS, 1.0f, ALL_SLOTS},
    {"honest, no slot", VMAF_TEST_ORDSUM_ADVICE_SUMS, 1.0f, 0u},
    {"honest, three slots", VMAF_TEST_ORDSUM_ADVICE_SUMS, 1.0f, 3u},
    {"half", VMAF_TEST_ORDSUM_ADVICE_SUMS, 0.5f, ALL_SLOTS},
    {"double", VMAF_TEST_ORDSUM_ADVICE_SUMS, 2.0f, ALL_SLOTS},
    {"a thousandth", VMAF_TEST_ORDSUM_ADVICE_SUMS, 1e-3f, ALL_SLOTS},
    {"a thousand times, three slots", VMAF_TEST_ORDSUM_ADVICE_SUMS, 1e3f, 3u},
    {"all zero", VMAF_TEST_ORDSUM_ADVICE_ZERO, 0.0f, ALL_SLOTS},
    {"constant", VMAF_TEST_ORDSUM_ADVICE_CONSTANT, 1.0f, ALL_SLOTS},
    {"NaN", VMAF_TEST_ORDSUM_ADVICE_NAN, 0.0f, ALL_SLOTS},
};

static double values[MAX_TERMS];
static uint64_t terms[MAX_TERMS];

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

/* Deterministic generator: the same terms on every host. */
static uint64_t rng_state = 0x9e3779b97f4a7c15ULL;

static uint64_t rng_next(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

/* A value in [0, 1). */
static double unit_random(void)
{
    return (double)(rng_next() >> 11) / 9007199254740992.0;
}

static double make_term(int kind, size_t index, size_t count)
{
    const double base = 1e-3 * unit_random();
    switch (kind) {
    case KIND_FOURTH:
        return (base * base) * (base * base);
    case KIND_SPARSE:
        return (rng_next() & 3u) != 0u ? 0.0 : base;
    case KIND_GROWING:
        return ldexp(1.0 + unit_random(), (int)(index * 300u / (count + 1u)) - 200);
    case KIND_TINY:
        return (base * 1e-12) * (base * 1e-12) * ((base * 1e-12) * (base * 1e-12));
    case KIND_NAN:
        return index == count / 2u ? (double)NAN : base;
    default:
        return base;
    }
}

/* The terms of a case and the loop's sum of them. */
static double fill_terms(int kind, size_t count)
{
    rng_state = 0x9e3779b97f4a7c15ULL;
    double sum = 0.0;
    for (size_t i = 0u; i < count; i++) {
        values[i] = make_term(kind, i, count);
        terms[i] = bits_of(values[i]);
    }
    for (size_t i = 0u; i < count; i++)
        sum += values[i];
    return sum;
}

static int same_sum(uint64_t got, double loop)
{
    if (isnan(loop))
        return isnan(value_of(got));
    return got == bits_of(loop);
}

/* One case through the probe; 0 when its sum is the loop's. */
static int check_case(int kind, size_t count, const Plan *plan, int on_device, size_t *asked)
{
    const double loop = fill_terms(kind, count);
    VmafTestOrdsumCase c = {
        .terms = terms,
        .count = count,
        .advice = plan->advice,
        .advice_factor = plan->factor,
        .scale_log2 = (kind == KIND_FOURTH || kind == KIND_TINY) ? FOURTH_SCALE_LOG2 : 0,
        .slot_limit = plan->slot_limit,
        .on_device = on_device,
    };
    const int err = vmaf_test_sycl_ordsum(&c);
    if (err)
        return err;
    if (asked)
        *asked = c.asked_terms;
    if (same_sum(c.sum, loop))
        return 0;
    (void)fprintf(stderr, "\n%s, %zu terms, advice %s, %s: %a, the loop gives %a\n",
                  KIND_NAME[kind], count, plan->what, on_device ? "device" : "host",
                  value_of(c.sum), loop);
    return 1;
}

/* Every plan on terms of one kind and length. */
static char *check_plans(int kind, size_t count)
{
    for (unsigned plan = 0u; plan < PLAN_COUNT; plan++) {
        mu_assert("the ordered sum is not the loop's sum",
                  check_case(kind, count, &PLAN[plan], 0, NULL) == 0);
    }
    return NULL;
}

static char *test_every_plan_gives_the_loops_sum(void)
{
    for (int kind = 0; kind < KIND_COUNT; kind++) {
        for (unsigned length = 0u; length < LENGTH_COUNT; length++)
            mu_assert_msg(check_plans(kind, LENGTH[length]));
    }
    return NULL;
}

/* With honest advice and all slots the walk computes no term itself on
 * ordinary sums; without slots, and where a sum crosses more binades than
 * there are slots, it does. Both halves of the walk are reached. */
static char *test_slots_decide_who_computes_the_terms(void)
{
    size_t asked = 0u;
    mu_assert("honest advice failed",
              check_case(KIND_UNIFORM, MAX_TERMS, &PLAN[0], 0, &asked) == 0);
    mu_assert("the walk computed terms although every crossing chunk had a slot", asked == 0u);
    mu_assert("no-slot advice failed",
              check_case(KIND_UNIFORM, MAX_TERMS, &PLAN[1], 0, &asked) == 0);
    mu_assert("the walk computed no term although no chunk had a slot", asked > 0u);
    mu_assert("growing terms failed",
              check_case(KIND_GROWING, MAX_TERMS, &PLAN[0], 0, &asked) == 0);
    mu_assert("300 binades fit the slots: the test no longer runs out of them", asked > 0u);
    return NULL;
}

static char *test_device_walk_gives_the_loops_sum(void)
{
    for (int kind = 0; kind < KIND_COUNT; kind++) {
        for (unsigned plan = 0u; plan < PLAN_COUNT; plan++) {
            const int err = check_case(kind, LENGTH[LENGTH_COUNT - 2u], &PLAN[plan], 1, NULL);
            if (err == -ENODEV) {
                (void)fprintf(stderr, "[skip: no SYCL GPU] ");
                mu_skipped = 1;
                return NULL;
            }
            mu_assert("the walk kernel failed or its sum is not the loop's", err == 0);
        }
    }
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_every_plan_gives_the_loops_sum);
    mu_run_test(test_slots_decide_who_computes_the_terms);
    mu_run_test(test_device_walk_gives_the_loops_sum);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
