/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * feature/ordered_sum.h against the loop it stands for (ADR-1433).
 *
 * The header reproduces `for (i = 0; i < n; i++) s += x[i];` over
 * non-negative doubles from chunk-wise integer pieces, so that a device can
 * form the pieces in parallel and still return the CPU's bits. This test
 * runs the same pipeline on the host, laid out the way the ssimulacra2 CUDA
 * kernels lay it out (lanes that take a run of consecutive terms, an ordered
 * tree over the lanes of a chunk, one walk over the chunks), and compares
 * the result with the plain loop bit for bit: random terms, terms that tie on
 * every add, zeros, a dynamic range of 600 binades, NaN and infinity, and
 * plans that are deliberately wrong. No device is needed.
 *
 * It also checks the one-term rule the rest is built on against the
 * floating-point add itself, and that the fixtures are order-sensitive at
 * all: a pairwise tree over the same terms ends on other bits.
 */

#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test.h"

#include "feature/ordered_sum.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy also proposes the `nullptr` keyword, but MSVC's
 * documented /std:clatest C23 feature set does not include `nullptr` while the
 * required Windows build compiles this TU with cl.exe. ADR-1138. */

/* The ssimulacra2 CUDA layout: 256 lanes, 4 consecutive terms per lane. */
#define LANES 256u
#define RUN 4u
#define CHUNK ((size_t)LANES * RUN)

#define MANY 300000u

enum plan_mode {
    PLAN_FROM_SUMS, /* vmaf_ordsum_plan over the chunks' approximate sums */
    PLAN_SHIFTED,   /* that plan, one binade too high on every chunk */
    PLAN_ALL_ZERO,  /* "every chunk is zero" */
    PLAN_FIXED,     /* one binade for every chunk */
};

typedef struct Fixture {
    double *x;
    size_t n;
} Fixture;

static uint64_t rng_state = 0x9e3779b97f4a7c15u;

static uint64_t rng_next(void)
{
    uint64_t z = (rng_state += 0x9e3779b97f4a7c15u);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9u;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebu;
    return z ^ (z >> 31);
}

/* Uniform in [0, 1) with 53 random bits. */
static double rng_unit(void)
{
    return (double)(rng_next() >> 11) * 0x1.0p-53;
}

static uint64_t bits_of(double v)
{
    uint64_t bits;
    memcpy(&bits, &v, sizeof(bits));
    return bits;
}

/* Same value, or both NaN. */
static bool same_double(double a, double b)
{
    if (isnan(a) || isnan(b))
        return isnan(a) && isnan(b);
    return bits_of(a) == bits_of(b);
}

static double loop_sum(const Fixture *f)
{
    double s = 0.0;
    for (size_t i = 0; i < f->n; i++)
        s += f->x[i];
    return s;
}

/* The same terms, added pairwise. */
static double tree_sum(const double *x, size_t n)
{
    if (n == 0u)
        return 0.0;
    double *level = malloc(n * sizeof(*level));
    if (!level)
        return NAN;
    memcpy(level, x, n * sizeof(*level));
    size_t count = n;
    while (count > 1u) {
        const size_t half = count / 2u;
        for (size_t i = 0; i < half; i++)
            level[i] = level[2u * i] + level[2u * i + 1u];
        if (count & 1u)
            level[half] = level[count - 1u];
        count = half + (count & 1u);
    }
    const double s = level[0];
    free(level);
    return s;
}

static double term_or_zero(const Fixture *f, size_t i)
{
    return i < f->n ? f->x[i] : 0.0;
}

/* Approximate sum of one chunk: each lane adds its run, then the lanes are
 * added in the halving tree of the device kernel. */
static double chunk_approximate_sum(const Fixture *f, size_t chunk)
{
    double lane_sum[LANES];
    for (unsigned lane = 0; lane < LANES; lane++) {
        double s = 0.0;
        for (unsigned j = 0; j < RUN; j++)
            s += term_or_zero(f, chunk * CHUNK + (size_t)lane * RUN + j);
        lane_sum[lane] = s;
    }
    for (unsigned half = LANES / 2u; half > 0u; half >>= 1) {
        for (unsigned lane = 0; lane < half; lane++)
            lane_sum[lane] += lane_sum[lane + half];
    }
    return lane_sum[0];
}

/* Increment of one chunk under `plan`: each lane composes its run in order,
 * then adjacent lanes are composed in an ordered tree. */
static VmafOrdsumUnits chunk_units(const Fixture *f, size_t chunk, int plan)
{
    VmafOrdsumUnits lane_units[LANES];
    for (unsigned lane = 0; lane < LANES; lane++) {
        VmafOrdsumUnits u = vmaf_ordsum_units(0, 0);
        for (unsigned j = 0; j < RUN; j++) {
            const double x = term_or_zero(f, chunk * CHUNK + (size_t)lane * RUN + j);
            u = vmaf_ordsum_then(u, vmaf_ordsum_planned_term(x, plan));
        }
        lane_units[lane] = u;
    }
    for (unsigned step = 1; step < LANES; step <<= 1) {
        for (unsigned lane = 0; lane < LANES; lane += 2u * step)
            lane_units[lane] = vmaf_ordsum_then(lane_units[lane], lane_units[lane + step]);
    }
    return lane_units[0];
}

static int plan_for_mode(enum plan_mode mode, int planned)
{
    switch (mode) {
    case PLAN_SHIFTED:
        return vmaf_ordsum_plan_is_binade(planned) && planned < VMAF_ORDSUM_MAX_EXP ? planned + 1 :
                                                                                      planned;
    case PLAN_ALL_ZERO:
        return VMAF_ORDSUM_PLAN_ZERO;
    case PLAN_FIXED:
        return -3;
    case PLAN_FROM_SUMS:
    default:
        return planned;
    }
}

/* Chunks of the last pipeline run whose increment depended on the parity of
 * the running integer, that is, chunks with a tie that mattered. */
static size_t parity_chunks;

/* The whole pipeline; `*fallbacks` counts the chunks added term by term. */
static double pipeline_sum(const Fixture *f, enum plan_mode mode, size_t *fallbacks)
{
    const size_t chunks = (f->n + CHUNK - 1u) / CHUNK;
    double prefix = 0.0;
    double s = 0.0;
    *fallbacks = 0u;
    parity_chunks = 0u;
    for (size_t chunk = 0; chunk < chunks; chunk++) {
        const int planned = vmaf_ordsum_plan(&prefix, chunk_approximate_sum(f, chunk));
        const int plan = plan_for_mode(mode, planned);
        const VmafOrdsumUnits units = chunk_units(f, chunk, plan);
        if (vmaf_ordsum_add_chunk(&s, plan, units)) {
            parity_chunks += units.even != units.odd ? 1u : 0u;
            continue;
        }
        (*fallbacks)++;
        for (size_t i = chunk * CHUNK; i < (chunk + 1u) * CHUNK; i++)
            s += term_or_zero(f, i);
    }
    return s;
}

static char *check_fixture(const Fixture *f, const char *what, size_t max_fallbacks)
{
    const double expect = loop_sum(f);
    for (int mode = PLAN_FROM_SUMS; mode <= PLAN_FIXED; mode++) {
        size_t fallbacks = 0u;
        const double got = pipeline_sum(f, (enum plan_mode)mode, &fallbacks);
        if (!same_double(expect, got)) {
            (void)fprintf(stderr, "\n%s, plan mode %d: loop %a pipeline %a\n", what, mode, expect,
                          got);
        }
        mu_assert("the pipeline's sum is not the loop's", same_double(expect, got));
        if (mode == PLAN_FROM_SUMS && fallbacks > max_fallbacks) {
            (void)fprintf(stderr, "\n%s: %zu chunks added term by term, at most %zu expected\n",
                          what, fallbacks, max_fallbacks);
        }
        mu_assert("too many chunks took the term-by-term path",
                  mode != PLAN_FROM_SUMS || fallbacks <= max_fallbacks);
    }
    return NULL;
}

static char *with_fixture(size_t n, void (*fill)(double *x, size_t n), const char *what,
                          size_t max_fallbacks)
{
    Fixture f = {.x = malloc((n ? n : 1u) * sizeof(double)), .n = n};
    mu_assert("allocation failed", f.x != NULL);
    fill(f.x, n);
    char *msg = check_fixture(&f, what, max_fallbacks);
    free(f.x);
    return msg;
}

/* --- fixtures ---------------------------------------------------------- */

static void fill_uniform(double *x, size_t n)
{
    for (size_t i = 0; i < n; i++)
        x[i] = rng_unit();
}

/* Terms like ssimulacra2's `1 - q` with q just below 1: multiples of 2^-53,
 * here up to 3e-5. The sum passes through [1, 2), where every odd multiple
 * ties, and [2, 4), where a quarter of the terms do. */
static void fill_ties(double *x, size_t n)
{
    for (size_t i = 0; i < n; i++)
        x[i] = (double)(1u + (rng_next() >> 26)) * 0x1.0p-53;
}

static void fill_zero(double *x, size_t n)
{
    for (size_t i = 0; i < n; i++)
        x[i] = 0.0;
}

/* A long run of zeros, then values, then zeros again. */
static void fill_zero_islands(double *x, size_t n)
{
    for (size_t i = 0; i < n; i++)
        x[i] = (i > n / 3u && i < n / 2u) ? rng_unit() * 1e-3 : 0.0;
}

/* Every term in a binade of its own choosing between 2^-300 and 2^300. */
static void fill_wide_range(double *x, size_t n)
{
    for (size_t i = 0; i < n; i++)
        x[i] = ldexp(1.0 + rng_unit(), (int)(rng_next() % 601u) - 300);
}

/* Fourth powers of small values: most are far below half a unit of the sum. */
static void fill_quartic(double *x, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        const double d = rng_unit() * ((rng_next() & 63u) == 0u ? 1.0 : 1e-5);
        x[i] = (d * d) * (d * d);
    }
}

static void fill_subnormal(double *x, size_t n)
{
    for (size_t i = 0; i < n; i++)
        x[i] = (double)(rng_next() & 0xffffu) * 0x1.0p-1074;
}

static void fill_nan_in_the_middle(double *x, size_t n)
{
    fill_uniform(x, n);
    x[n / 2u] = NAN;
}

static void fill_infinity_then_nan(double *x, size_t n)
{
    fill_uniform(x, n);
    x[n / 4u] = INFINITY;
    x[n - n / 4u] = NAN;
}

static void fill_infinity(double *x, size_t n)
{
    fill_uniform(x, n);
    x[n / 3u] = INFINITY;
}

/* --- tests ------------------------------------------------------------- */

/* The rule the header is built on, against the add itself: a sum `s` in
 * binade e plus a term is `s` moved by the term's increment, whenever the
 * result stays in the binade. */
static char *test_one_term_is_the_floating_point_add(void)
{
    unsigned checked = 0;
    for (unsigned k = 0; k < 2000000u; k++) {
        const int e = (int)(rng_next() % 80u) - 40;
        const double s = ldexp(1.0 + rng_unit(), e);
        /* Terms from 60 binades below the sum up to its own, half of them
         * with their low bits cleared so that ties occur. */
        double x = ldexp(1.0 + rng_unit(), e - (int)(rng_next() % 61u));
        if (k & 1u)
            x = ldexp(floor(ldexp(x, 53 - e)), e - 53);
        const double expect = s + x;
        if (!(expect <= ldexp(1.0, e + 1)))
            continue;
        const VmafOrdsumUnits u = vmaf_ordsum_term(x, e);
        double got = s;
        mu_assert("a term that keeps the sum in its binade was not added",
                  vmaf_ordsum_add_chunk(&got, e, u));
        mu_assert("one term's increment is not the floating-point add", same_double(expect, got));
        checked++;
    }
    mu_assert("too few samples stayed in their binade", checked > 500000u);
    return NULL;
}

/* A term the integer form cannot take sends the chunk to the terms. */
static char *test_unfit_terms_are_refused(void)
{
    const double unfit[] = {-1.0, NAN, INFINITY, -INFINITY, 4.0};
    for (size_t i = 0; i < sizeof(unfit) / sizeof(unfit[0]); i++) {
        double s = 1.5;
        mu_assert("an unfit term was added in integers",
                  !vmaf_ordsum_add_chunk(&s, 0, vmaf_ordsum_term(unfit[i], 0)));
        mu_assert("a refused chunk changed the sum", s == 1.5);
    }
    double s = 1.5;
    mu_assert("a negative zero is a zero",
              vmaf_ordsum_add_chunk(&s, 0, vmaf_ordsum_term(-0.0, 0)) && s == 1.5);
    mu_assert("a non-zero term passed as part of an all-zero chunk",
              !vmaf_ordsum_add_chunk(&s, VMAF_ORDSUM_PLAN_ZERO,
                                     vmaf_ordsum_planned_term(0x1.0p-200, VMAF_ORDSUM_PLAN_ZERO)));
    return NULL;
}

/* The fixtures are worth the trouble: a pairwise tree ends on other bits. */
static char *test_fixture_is_order_sensitive(void)
{
    Fixture f = {.x = malloc(MANY * sizeof(double)), .n = MANY};
    mu_assert("allocation failed", f.x != NULL);
    fill_uniform(f.x, f.n);
    const bool differs = !same_double(loop_sum(&f), tree_sum(f.x, f.n));
    free(f.x);
    mu_assert("a pairwise tree gives the loop's bits on this fixture", differs);
    return NULL;
}

static char *test_uniform(void)
{
    return with_fixture(MANY, fill_uniform, "uniform", 40u);
}

/* The tie rule decides here: most chunks' increments depend on the parity
 * they start from. */
static char *test_ties(void)
{
    Fixture f = {.x = malloc(MANY * sizeof(double)), .n = MANY};
    mu_assert("allocation failed", f.x != NULL);
    fill_ties(f.x, f.n);
    char *msg = check_fixture(&f, "ties", 40u);
    size_t fallbacks = 0u;
    const double s = pipeline_sum(&f, PLAN_FROM_SUMS, &fallbacks);
    free(f.x);
    mu_assert_msg(msg);
    mu_assert("the tie fixture's sum did not pass through [1, 4)", s > 4.0);
    mu_assert("too few chunks of the tie fixture depend on the parity", parity_chunks > 100u);
    return NULL;
}

static char *test_zero(void)
{
    return with_fixture(MANY, fill_zero, "zero", 0u);
}

static char *test_zero_islands(void)
{
    return with_fixture(MANY, fill_zero_islands, "zero islands", 40u);
}

static char *test_wide_range(void)
{
    return with_fixture(MANY, fill_wide_range, "wide range", 60u);
}

static char *test_quartic(void)
{
    return with_fixture(MANY, fill_quartic, "quartic", 80u);
}

static char *test_subnormal(void)
{
    return with_fixture(20000u, fill_subnormal, "subnormal", 20u);
}

static char *test_nan(void)
{
    return with_fixture(MANY, fill_nan_in_the_middle, "NaN", 41u);
}

static char *test_infinity_then_nan(void)
{
    return with_fixture(MANY, fill_infinity_then_nan, "infinity then NaN", 42u);
}

static char *test_infinity(void)
{
    return with_fixture(MANY, fill_infinity, "infinity", 41u);
}

/* Shorter than a chunk, one term, none, and a length that leaves a partial
 * last chunk. */
static char *test_lengths(void)
{
    const size_t lengths[] = {0u, 1u, 7u, CHUNK - 1u, CHUNK, CHUNK + 1u, 5u * CHUNK + 333u};
    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++)
        mu_assert_msg(with_fixture(lengths[i], fill_uniform, "length", 12u));
    return NULL;
}

static char *run_rule_tests(void)
{
    mu_run_test(test_one_term_is_the_floating_point_add);
    mu_run_test(test_unfit_terms_are_refused);
    mu_run_test(test_fixture_is_order_sensitive);
    return NULL;
}

static char *run_value_tests(void)
{
    mu_run_test(test_uniform);
    mu_run_test(test_ties);
    mu_run_test(test_zero);
    mu_run_test(test_zero_islands);
    mu_run_test(test_wide_range);
    mu_run_test(test_quartic);
    mu_run_test(test_subnormal);
    return NULL;
}

static char *run_edge_tests(void)
{
    mu_run_test(test_nan);
    mu_run_test(test_infinity_then_nan);
    mu_run_test(test_infinity);
    mu_run_test(test_lengths);
    return NULL;
}

char *run_tests(void)
{
    /* The reference here is a plain C loop over doubles. On a target that
     * evaluates double expressions in a wider format (x87) that loop is not
     * the IEEE binary64 sum the header reproduces, so there is nothing to
     * compare against. */
    if (FLT_EVAL_METHOD != 0) {
        (void)fprintf(stderr, "[skip: FLT_EVAL_METHOD is %d] ", (int)FLT_EVAL_METHOD);
        mu_skipped = 1;
        return NULL;
    }
    mu_assert_msg(run_rule_tests());
    mu_assert_msg(run_value_tests());
    mu_assert_msg(run_edge_tests());
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
