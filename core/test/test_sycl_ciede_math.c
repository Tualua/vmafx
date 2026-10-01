/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * ADR-1436 — the arithmetic ciede_sycl's kernel runs, on the host and in
 * kernels on the default GPU.
 *
 *   - The fp32 pair functions of feature/sycl/sycl_ff_math.h (sqrt, cbrt,
 *     x^2.4, x^7, exp, sin, cos, atan2) against the host's extended-precision
 *     math library, over the arguments ciede2000 passes them. A SYCL kernel
 *     has no fp64 type (ADR-0220); a pair carries about 48 bits, and each
 *     function must return its value to 2^-42.
 *   - The ciede2000 pixel of feature/sycl/sycl_ciede_math.h against the same
 *     statements in fp64 (cuda/integer_ciede/ciede_device.h compiled for the
 *     host, which test_ciede_device_math holds to the CPU extractor). A
 *     pixel's float may differ where a pair does not decide a rounding or the
 *     host's math library is not correctly rounded: a few pixels in a
 *     million, each by about one float step. An fp32 evaluation of the
 *     formula, which the twin had before, differs on most pixels.
 *
 * The device half exits 77 without a GPU; the host half always runs.
 */

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test.h"

#include "feature/cuda/integer_ciede/ciede_device.h"
#include "sycl_ciede_math_probe.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy proposes `nullptr`, but the required MSVC C build does
 * not provide that keyword. Preserve the portable C spelling. ADR-1138. */

#define FF_SAMPLES 200000u
#define PIXEL_SAMPLES 150000u
/* A pair function's largest error, as a fraction of its value: 2^-42. The
 * functions measure 2^-44 to 2^-47. */
#define FF_TOLERANCE 0x1p-42L
/* Pixels whose float may differ from the fp64 evaluation, per batch: measured
 * 0.6 per million. */
#define PIXEL_DIFFERING_MAX 12u
/* And how far the batch's sum may move, as a fraction of it. */
#define PIXEL_SUM_TOLERANCE 1e-9

/* Deterministic generator: the same inputs on every host. */
static uint64_t rng_state = 0x9E3779B97F4A7C15ull;

static uint64_t rng_next(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

/* Uniform in [0, 1). */
static double rng_unit(void)
{
    return (double)(rng_next() >> 11) * 0x1p-53;
}

static uint32_t bits_of(float x)
{
    uint32_t bits;
    memcpy(&bits, &x, sizeof(bits));
    return bits;
}

/* Set when a device entry point found no GPU. */
static bool no_device = false;

/* The operands and results of one batch of a pair function. */
typedef struct FfBuffers {
    float *a;
    float *b;
    float *hi;
    float *lo;
    int32_t *exponent;
} FfBuffers;

static int ff_alloc(FfBuffers *f, size_t n)
{
    f->a = calloc(n, sizeof(float));
    f->b = calloc(n, sizeof(float));
    f->hi = calloc(n, sizeof(float));
    f->lo = calloc(n, sizeof(float));
    f->exponent = calloc(n, sizeof(int32_t));
    return (f->a && f->b && f->hi && f->lo && f->exponent) ? 0 : -1;
}

static void ff_free(FfBuffers *f)
{
    free(f->a);
    free(f->b);
    free(f->hi);
    free(f->lo);
    free(f->exponent);
}

/* A pair operand uniform in [lo, hi): a double split into two floats. */
static void pair_operand(FfBuffers *f, size_t i, double lo, double hi)
{
    const double v = lo + (hi - lo) * rng_unit();
    f->a[i] = (float)v;
    f->b[i] = (float)(v - (double)f->a[i]);
}

/* Operands over what ciede2000 passes each function. */
static void ff_fill(FfBuffers *f, int function, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        switch (function) {
        case VMAF_TEST_FF_SQRT: /* sums of squares of Lab coordinates, ratios below one */
            pair_operand(f, i, (i & 1u) ? 1e-6 : 0.5, (i & 1u) ? 1.0 : 4e4);
            break;
        case VMAF_TEST_FF_CBRT: /* above 216 / 24389 */
            pair_operand(f, i, 0.0088, 1.3);
            break;
        case VMAF_TEST_FF_POW_2_4: /* (c + 0.055) / 1.055 for c above 10 / 255 */
            pair_operand(f, i, 0.089, 1.3);
            break;
        case VMAF_TEST_FF_POW_7: /* chroma */
            f->a[i] = (float)(0.01 + 200.0 * rng_unit());
            f->b[i] = 0.0f;
            break;
        case VMAF_TEST_FF_EXP: /* -((h - 275) / 25)^2 for h in 0 .. 360 degrees */
            f->a[i] = (float)(-121.0 * rng_unit());
            f->b[i] = 0.0f;
            break;
        case VMAF_TEST_FF_SIN:
        case VMAF_TEST_FF_COS: /* up to 4 h + ..., h below 2 pi */
            pair_operand(f, i, -30.0, 30.0);
            break;
        default: /* atan2: Lab b and a', with points on and next to the axes */
            f->a[i] = (float)(200.0 * (rng_unit() - 0.5));
            f->b[i] = (float)(200.0 * (rng_unit() - 0.5));
            if (rng_next() % 16u == 0u)
                f->b[i] = 0.0f;
            if (rng_next() % 16u == 0u)
                f->a[i] = (float)(1e-3 * (rng_unit() - 0.5));
            if (f->a[i] == 0.0f && f->b[i] == 0.0f)
                f->a[i] = 1.0f;
            break;
        }
    }
}

/* The function's value at the operands, from the host's extended-precision
 * math library. */
static long double ff_reference(int function, float a, float b)
{
    const long double pair = (long double)a + (long double)b;
    switch (function) {
    case VMAF_TEST_FF_SQRT:
        return sqrtl(pair);
    case VMAF_TEST_FF_CBRT:
        return cbrtl(pair);
    case VMAF_TEST_FF_POW_2_4:
        return powl(pair, 2.4L);
    case VMAF_TEST_FF_POW_7:
        return powl((long double)a, 7.0L);
    case VMAF_TEST_FF_EXP:
        return expl((long double)a);
    case VMAF_TEST_FF_SIN:
        return sinl(pair);
    case VMAF_TEST_FF_COS:
        return cosl(pair);
    default:
        return atan2l((long double)a, (long double)b);
    }
}

/* Every result of the batch within FF_TOLERANCE of the reference: relative
 * to the value, and for sin and cos to one, which bounds them. */
static char *ff_check(const FfBuffers *f, int function, size_t n, const char *where)
{
    static const char *const names[VMAF_TEST_FF_COUNT] = {"sqrt", "cbrt", "pow_2_4", "pow_7",
                                                          "exp",  "sin",  "cos",     "atan2"};
    long double worst = 0.0L;
    for (size_t i = 0; i < n; i++) {
        const long double want = ff_reference(function, f->a[i], f->b[i]);
        const long double got =
            ldexpl((long double)f->hi[i] + (long double)f->lo[i], (int)f->exponent[i]);
        const bool bounded = function == VMAF_TEST_FF_SIN || function == VMAF_TEST_FF_COS;
        const long double scale = bounded ? 1.0L : fabsl(want);
        const long double error = (scale > 0.0L) ? fabsl(got - want) / scale : fabsl(got);
        if (error > worst)
            worst = error;
    }
    if (!(worst <= FF_TOLERANCE)) {
        (void)fprintf(stderr, "\n%s %s: largest error 2^%.1f, allowed 2^-42\n", where,
                      names[function], (double)log2l(worst));
    }
    mu_assert("a pair function is less accurate than 2^-42", worst <= FF_TOLERANCE);
    return NULL;
}

static char *check_pair_functions(bool on_device, const char *where)
{
    FfBuffers f;
    if (ff_alloc(&f, FF_SAMPLES) != 0) {
        ff_free(&f);
        return "allocation failed";
    }
    char *msg = NULL;
    for (int function = 0; function < VMAF_TEST_FF_COUNT && !msg && !no_device; function++) {
        ff_fill(&f, function, FF_SAMPLES);
        const VmafTestFfBatch batch = {.function = function,
                                       .a = f.a,
                                       .b = f.b,
                                       .n = FF_SAMPLES,
                                       .hi = f.hi,
                                       .lo = f.lo,
                                       .exponent = f.exponent};
        const int err = vmaf_test_sycl_ff_batch(&batch, on_device ? 1 : 0);
        if (err == -ENODEV) {
            no_device = true;
        } else if (err) {
            msg = "the pair-function batch failed";
        } else {
            msg = ff_check(&f, function, FF_SAMPLES, where);
        }
    }
    ff_free(&f);
    return msg;
}

/* Six samples per pixel at `bpc` bits: a reference colour, and a distorted
 * one a few levels away (as in coded video) or, for one pixel in eight,
 * anywhere. One pixel in sixteen is identical. */
static void pixel_fill(float *samples, size_t n, unsigned bpc)
{
    const long peak = (1L << bpc) - 1L;
    const long step = 1L << (bpc - 8u);
    for (size_t i = 0; i < n; i++) {
        float *s = samples + 6u * i;
        const unsigned kind = (unsigned)(rng_next() % 16u);
        for (int j = 0; j < 3; j++) {
            const long v = (long)(rng_next() % (uint64_t)(peak + 1L));
            long d = v;
            if (kind >= 2u) {
                d = v + ((long)(rng_next() % 41u) - 20L) * step / 4L;
            } else if (kind == 1u) {
                d = (long)(rng_next() % (uint64_t)(peak + 1L));
            }
            d = d < 0L ? 0L : (d > peak ? peak : d);
            s[j] = (float)v;
            s[3 + j] = (float)d;
        }
    }
}

/* The batch against the fp64 statements of the reference. */
static char *pixel_check(const float *samples, const float *got, size_t n, unsigned bpc,
                         const char *where)
{
    unsigned differing = 0u;
    double sum_want = 0.0;
    double sum_got = 0.0;
    for (size_t i = 0; i < n; i++) {
        const float *s = samples + 6u * i;
        const float want = ciede_pixel(s[0], s[1], s[2], s[3], s[4], s[5], bpc);
        sum_want += (double)want;
        sum_got += (double)got[i];
        if (bits_of(want) == bits_of(got[i]))
            continue;
        if (differing++ < 4u) {
            (void)fprintf(stderr,
                          "\n%s %u-bit pixel (%g %g %g / %g %g %g): fp64 %.9g, pairs %.9g\n", where,
                          bpc, (double)s[0], (double)s[1], (double)s[2], (double)s[3], (double)s[4],
                          (double)s[5], (double)want, (double)got[i]);
        }
    }
    if (differing > PIXEL_DIFFERING_MAX) {
        (void)fprintf(stderr, "\n%s %u-bit: %u of %zu pixels differ from the fp64 evaluation\n",
                      where, bpc, differing, n);
    }
    mu_assert("more pixels differ from the fp64 evaluation than pair rounding explains",
              differing <= PIXEL_DIFFERING_MAX);
    mu_assert("the sum of the batch moved by more than a few float steps",
              fabs(sum_got - sum_want) <= PIXEL_SUM_TOLERANCE * sum_want);
    return NULL;
}

static char *check_pixels(bool on_device, const char *where)
{
    static const unsigned depths[4] = {8u, 10u, 12u, 16u};
    float *samples = calloc(6u * (size_t)PIXEL_SAMPLES, sizeof(float));
    float *delta_e = calloc(PIXEL_SAMPLES, sizeof(float));
    char *msg = (samples && delta_e) ? NULL : "allocation failed";
    for (int d = 0; d < 4 && !msg && !no_device; d++) {
        pixel_fill(samples, PIXEL_SAMPLES, depths[d]);
        const VmafTestCiedeBatch batch = {
            .samples = samples, .n = PIXEL_SAMPLES, .bpc = depths[d], .delta_e = delta_e};
        const int err = vmaf_test_sycl_ciede_batch(&batch, on_device ? 1 : 0);
        if (err == -ENODEV) {
            no_device = true;
        } else if (err) {
            msg = "the pixel batch failed";
        } else {
            msg = pixel_check(samples, delta_e, PIXEL_SAMPLES, depths[d], where);
        }
    }
    free(samples);
    free(delta_e);
    return msg;
}

static char *test_pair_functions_host(void)
{
    return check_pair_functions(false, "host");
}

static char *test_pixels_host(void)
{
    return check_pixels(false, "host");
}

static char *test_pair_functions_device(void)
{
    return check_pair_functions(true, "device");
}

static char *test_pixels_device(void)
{
    return no_device ? NULL : check_pixels(true, "device");
}

char *run_tests(void)
{
    mu_run_test(test_pair_functions_host);
    mu_run_test(test_pixels_host);
    mu_run_test(test_pair_functions_device);
    mu_run_test(test_pixels_device);
    if (no_device) {
        (void)fprintf(stderr, "[skip: no SYCL GPU for the device half] ");
        mu_skipped = 1;
    }
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
