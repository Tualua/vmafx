/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * The split SpEED covariance of the SYCL twin (ADR-1931) against speed.c,
 * device-free.
 *
 * speed_sycl_pipeline.cpp forms the reference's differences and products in
 * parallel, stores each as its fp64 bit pattern, and adds the stored terms
 * in one sequential chain per entry, in slices of submatrix rows with the
 * running sum handed on as its bit pattern. This test runs that split form on
 * the host (test_sycl_speed_cov_chain_probe.cpp, built like the extractor)
 * and requires, with `==` on bit patterns:
 *
 *   - the chain's add (signed_add() on a stored term) to equal the native
 *     fp64 add on ties with even and odd kept parts, carries into the next
 *     binade, cancellation to +0, sign changes, operands far apart and a
 *     seeded random sweep; the storage of a value to round-trip;
 *   - the split entry to equal (float)(compute_cov_kernel_scalar() / n) on
 *     every flat and cancelling block of speed_cov_cases.h, on all 325
 *     entries of the frame-140 U-reference plane (speed_cov_frame140_plane.h;
 *     entry (14, 11) is 0x3ae6645f), and on a seeded sweep of blocks with
 *     mixed magnitudes, at several slice heights; covariance_entry() (A)
 *     too, on the fixture.
 *
 * The design has no range bound and no fallback: the fallback count it
 * reports is 0 by construction.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test.h"

#include "feature/speed_cov.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. ADR-1138. */

uint64_t vmaf_test_cov_soft_add(uint64_t a, uint64_t b);
uint64_t vmaf_test_cov_bits_round_trip(uint64_t bits);
float vmaf_test_cov_entry_a(const float *x, const float *y, size_t stride, uint32_t width,
                            uint32_t height, float mean_x, float mean_y);
float vmaf_test_cov_split_entry(const float *x, const float *y, size_t stride, uint32_t width,
                                uint32_t height, float mean_x, float mean_y, uint32_t slice_rows,
                                uint64_t *scratch);

#include "speed_cov_cases.h"
#include "speed_cov_frame140_plane.h"

enum {
    F140_BLOCK = 5,
    F140_ELEMENTS = F140_BLOCK * F140_BLOCK,
    F140_SUB_W = SPEED_COV_FRAME140_WIDTH - F140_BLOCK + 1,
    F140_SUB_H = SPEED_COV_FRAME140_HEIGHT - F140_BLOCK + 1,
    SWEEP_BLOCKS = 120000,
    SWEEP_MAX_W = 9,
    SWEEP_MAX_H = 6,
    RANDOM_ADDS = 2000000,
};

static uint64_t scratch[3u * F140_SUB_W * F140_SUB_H];

static uint64_t bits_of(double v)
{
    uint64_t b;
    memcpy(&b, &v, sizeof(b));
    return b;
}

static double from_bits(uint64_t b)
{
    double v;
    memcpy(&v, &b, sizeof(v));
    return v;
}

static uint32_t float_bits(float v)
{
    uint32_t b;
    memcpy(&b, &v, sizeof(b));
    return b;
}

/* The native fp64 add against the chain's add; 1 when they differ. */
static int add_differs(double a, double b)
{
    const volatile double sum = a + b;
    const uint64_t chain = vmaf_test_cov_soft_add(bits_of(a), bits_of(b));
    if (chain != bits_of(sum)) {
        (void)fprintf(stderr, "[add %a + %a: got %a want %a] ", a, b, from_bits(chain), sum);
        return 1;
    }
    return 0;
}

static char *test_chain_add_constructed(void)
{
    const double one = 1.0;
    const double ulp = ldexp(1.0, -52);
    const double cases[][2] = {
        {one, ldexp(1.0, -53)},                    /* tie, even kept part: stays */
        {one + ulp, ldexp(1.0, -53)},              /* tie, odd kept part: rounds up */
        {one, ldexp(1.0, -53) + ldexp(1.0, -100)}, /* above half */
        {one, ldexp(1.0, -54)},                    /* below half */
        {2.0 - ulp, ulp},                          /* carry into the next binade */
        {2.0 - ulp, 2.0 - ulp},                    /* carry with rounding */
        {ldexp(1.0, 52) + 1.0, 0.5},               /* tie at an integer ulp, odd */
        {ldexp(1.0, 52), 0.5},                     /* tie at an integer ulp, even */
        {3.25, -3.25},                             /* cancellation to +0 */
        {-3.25, 3.25},                             /* cancellation to +0, negative first */
        {0.0, -7.5},                               /* +0 plus a negative term */
        {1.0, -1.0 - ulp},                         /* sign change, exact */
        {ldexp(1.0, 40), -ldexp(1.0, 40) - 1.0},   /* sign change across binades */
        {1.0, -ldexp(1.0, -53)},                   /* below a power of two, tie */
        {1.0, -ldexp(1.0, -54)},                   /* below a power of two, quarter */
        {1.0, ldexp(1.0, -80)},                    /* far apart: sticky only */
        {1.0, -ldexp(1.0, -80)},                   /* far apart, subtracted */
        {ldexp(1.0, 300), ldexp(-1.0, -300)},      /* very far apart */
        {9.379365163389542, -2770.576100301008},   /* magnitudes of the fixture */
    };
    unsigned wrong = 0u;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        wrong += (unsigned)add_differs(cases[i][0], cases[i][1]);
        wrong += (unsigned)add_differs(cases[i][1], cases[i][0]);
        wrong += (unsigned)add_differs(-cases[i][0], -cases[i][1]);
    }
    mu_assert("the chain's add differs from the fp64 add on a constructed case", wrong == 0u);
    return NULL;
}

static uint64_t sweep_state = 0x2545f4914f6cdd1dULL;

static uint64_t next_u64(void)
{
    sweep_state ^= sweep_state << 13;
    sweep_state ^= sweep_state >> 7;
    sweep_state ^= sweep_state << 17;
    return sweep_state;
}

/* A normal double with a random significand, sign and exponent in [-e, e]. */
static double random_double(int e)
{
    const double m = (double)((next_u64() >> 11) | (1ULL << 52)) * ldexp(1.0, -52);
    const int exp = (int)(next_u64() % (uint64_t)(2 * e + 1)) - e;
    const double v = ldexp(m, exp);
    return (next_u64() & 1u) ? -v : v;
}

static char *test_chain_add_random(void)
{
    unsigned wrong = 0u;
    for (unsigned i = 0u; i < RANDOM_ADDS && wrong < 8u; i++) {
        const double a = random_double(40);
        /* b near a's scale half of the time, so cancellation and ties occur */
        const double b = (i & 1u) ? random_double(40) : -a * (1.0 + random_double(30) * 1e-9);
        wrong += (unsigned)add_differs(a, b);
    }
    mu_assert("the chain's add differs from the fp64 add on the random sweep", wrong == 0u);
    for (unsigned i = 0u; i < 100000u; i++) {
        const uint64_t b = bits_of(random_double(1000));
        mu_assert("a stored fp64 value does not round-trip", vmaf_test_cov_bits_round_trip(b) == b);
    }
    mu_assert("+0 does not round-trip", vmaf_test_cov_bits_round_trip(0u) == 0u);
    return NULL;
}

static float reference(const float *x, const float *y, size_t stride, uint32_t w, uint32_t h,
                       float mx, float my)
{
    const double sum = compute_cov_kernel_scalar(x, y, stride, h, w, (double)mx, (double)my);
    return (float)(sum / (w * h));
}

static char *test_cases_blocks(void)
{
    build_cases();
    const uint32_t slices[] = {1u, 2u, HEIGHT};
    for (size_t s = 0; s < sizeof(slices) / sizeof(slices[0]); s++) {
        for (size_t c = 0; c < CASES; c++) {
            got[c] = vmaf_test_cov_split_entry(xs + c * BLOCK, ys + c * BLOCK, STRIDE, WIDTH,
                                               HEIGHT, mxs[c], mys[c], slices[s], scratch);
        }
        mu_assert("the split covariance differs from compute_cov_kernel_scalar() on a block of "
                  "speed_cov_cases.h",
                  count_wrong("split") == 0u);
    }
    (void)fprintf(stderr, "[%d blocks, %u discriminating, fallback 0] ", CASES, discriminating);
    return NULL;
}

/* speed.c's compute_mean() is static, so its statements are repeated here:
 * an fp32 running sum in raster order, then an fp32 quotient by the count. */
static float frame140_mean(uint32_t element)
{
    const float *base = speed_cov_frame140_plane +
                        ((size_t)(element / F140_BLOCK) * SPEED_COV_FRAME140_WIDTH) +
                        (element % F140_BLOCK);
    float result = 0.0f;
    for (size_t i = 0; i < F140_SUB_H; i++) {
        for (size_t j = 0; j < F140_SUB_W; j++) {
            result += base[i * SPEED_COV_FRAME140_WIDTH + j];
        }
    }
    return result / (float)(F140_SUB_W * F140_SUB_H);
}

static const float *frame140_block(uint32_t element)
{
    return speed_cov_frame140_plane + ((size_t)(element / F140_BLOCK) * SPEED_COV_FRAME140_WIDTH) +
           (element % F140_BLOCK);
}

static unsigned frame140_entry(uint32_t x, uint32_t y, const float *means, uint32_t slice_rows)
{
    const float want_v = reference(frame140_block(x), frame140_block(y), SPEED_COV_FRAME140_WIDTH,
                                   F140_SUB_W, F140_SUB_H, means[x], means[y]);
    const float split =
        vmaf_test_cov_split_entry(frame140_block(x), frame140_block(y), SPEED_COV_FRAME140_WIDTH,
                                  F140_SUB_W, F140_SUB_H, means[x], means[y], slice_rows, scratch);
    unsigned wrong = float_bits(split) != float_bits(want_v);
    if (slice_rows == F140_SUB_H) {
        const float a =
            vmaf_test_cov_entry_a(frame140_block(x), frame140_block(y), SPEED_COV_FRAME140_WIDTH,
                                  F140_SUB_W, F140_SUB_H, means[x], means[y]);
        wrong += float_bits(a) != float_bits(want_v);
    }
    if (x == 14u && y == 11u && slice_rows == F140_SUB_H) {
        (void)fprintf(stderr, "[frame 140 entry (14,11): ref 0x%08x split 0x%08x] ",
                      float_bits(want_v), float_bits(split));
        wrong += float_bits(split) != 0x3ae6645fu;
    }
    if (wrong != 0u) {
        (void)fprintf(stderr, "[frame 140 entry (%u,%u) slice %u differs] ", x, y, slice_rows);
    }
    return wrong;
}

static char *test_frame140_plane(void)
{
    float means[F140_ELEMENTS];
    for (uint32_t e = 0; e < F140_ELEMENTS; e++) {
        means[e] = frame140_mean(e);
    }
    const uint32_t slices[] = {1u, 7u, F140_SUB_H};
    unsigned entries = 0u;
    unsigned wrong = 0u;
    for (size_t s = 0; s < sizeof(slices) / sizeof(slices[0]); s++) {
        for (uint32_t x = 0; x < F140_ELEMENTS; x++) {
            for (uint32_t y = 0; y <= x; y++) {
                wrong += frame140_entry(x, y, means, slices[s]);
                entries += slices[s] == F140_SUB_H;
            }
        }
    }
    (void)fprintf(stderr, "[frame 140: %u entries, fallback 0] ", entries);
    mu_assert("the frame-140 fixture does not have 325 entries", entries == 325u);
    mu_assert("the split covariance differs from compute_cov_kernel_scalar() on the frame-140 "
              "plane",
              wrong == 0u);
    return NULL;
}

/* A sample with a 24-bit significand at a random scale, so the terms of one
 * block span many binades and their sums round in many places. */
static float sweep_sample(int scale)
{
    const double m = (double)((next_u64() >> 40) | (1ULL << 23)) * ldexp(1.0, -23);
    const int exp = (int)(next_u64() % (uint64_t)(2 * scale + 1)) - scale;
    const float v = (float)ldexp(m, exp);
    return (next_u64() & 1u) ? -v : v;
}

static unsigned sweep_block(float *x, float *y, uint32_t w, uint32_t h, float mx, float my,
                            int scale)
{
    for (uint32_t k = 0; k < w * h; k++) {
        x[k] = mx + sweep_sample(scale);
        y[k] = (k & 1u) ? my - (x[k] - mx) : my + sweep_sample(scale);
    }
    const float want_v = reference(x, y, w, w, h, mx, my);
    const uint32_t slice = 1u + (uint32_t)(next_u64() % h);
    const float split = vmaf_test_cov_split_entry(x, y, w, w, h, mx, my, slice, scratch);
    if (float_bits(split) != float_bits(want_v)) {
        (void)fprintf(stderr, "[sweep %ux%u: got %a want %a] ", w, h, (double)split,
                      (double)want_v);
        return 1u;
    }
    return 0u;
}

static char *test_random_blocks(void)
{
    static float x[SWEEP_MAX_W * SWEEP_MAX_H];
    static float y[SWEEP_MAX_W * SWEEP_MAX_H];
    unsigned wrong = 0u;
    for (unsigned b = 0u; b < SWEEP_BLOCKS && wrong < 8u; b++) {
        const uint32_t w = 1u + (uint32_t)(next_u64() % SWEEP_MAX_W);
        const uint32_t h = 1u + (uint32_t)(next_u64() % SWEEP_MAX_H);
        const int scale = 1 + (int)(next_u64() % 24u);
        const float mx = sweep_sample(12);
        const float my = sweep_sample(12);
        wrong += sweep_block(x, y, w, h, mx, my, scale);
    }
    (void)fprintf(stderr, "[%d random blocks, fallback 0] ", SWEEP_BLOCKS);
    mu_assert("the split covariance differs from compute_cov_kernel_scalar() on the sweep",
              wrong == 0u);
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_chain_add_constructed);
    mu_run_test(test_chain_add_random);
    mu_run_test(test_cases_blocks);
    mu_run_test(test_frame140_plane);
    mu_run_test(test_random_blocks);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
