/**
 *  Copyright 2016-2026 Netflix, Inc.
 *  Copyright (c) 2019 Joshua Holmer
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: BSD-2-Clause-Patent AND MIT
 */

/*
 * ADR-1476: ciede2000() forms two products in float, as Netflix/vmaf does.
 *
 *   libvmaf/src/feature/ciede.c:224-225
 *       const float delta_upcase_h_prime =
 *               2.0 * sqrt(c_prime_1 * c_prime_2) * sin(delta_h_prime / 2.0);
 *   libvmaf/src/feature/ciede.c:235-236
 *       return sqrt(pow(lightness, 2) + pow(chroma, 2) +
 *                   pow(hue, 2) + r_sub_t * chroma * hue);
 *
 * `c_prime_1 * c_prime_2` and `r_sub_t * chroma * hue` have float operands, so
 * C evaluates them in float and rounds each product to float before the
 * surrounding fp64 expression widens it. The fork cast the first operand of
 * both to double between PR #552 and ADR-1476, which kept the products exact
 * and moved `ciede2000` away from upstream on 265 of 327 measured frames.
 *
 * cuda/integer_ciede/ciede_device.h is ciede.c's ciede2000() statement for
 * statement, and test_ciede_device_math holds it to the CPU extractor bit for
 * bit. This test holds the header to upstream's two products: it replays
 * ciede_delta_e() with the header's own helpers, once with float products and
 * once with widened ones, over a grid of colour pairs. The header has to
 * return the float form on every pair, and the grid has to tell the two forms
 * apart, so the test fails on a widened product whatever the math library
 * rounds like. Together the two tests pin the products of ciede.c.
 */

#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#include "test.h"

#include "feature/cuda/integer_ciede/ciede_device.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. ADR-1138. */

/* (float)sqrt(pow(a, 2) + pow(b, 2)), as ciede2000() writes c1, c2 and the
 * two c_prime values. */
static float chroma_of(float a, float b)
{
    return (float)sqrt(ciede_sq(a) + ciede_sq(b));
}

/* a + (a / 2) * (1 - sqrt(c_bar^7 / (c_bar^7 + 25^7))) */
static float a_prime_of(float a, float c_bar)
{
    const double c_bar_7 = pow((double)c_bar, 7.0);
    const double g_factor = 1.0 - sqrt(c_bar_7 / (c_bar_7 + CIEDE_POW_25_7));
    return (float)((double)a + (double)(a / 2.0f) * g_factor);
}

/* ciede2000() with ksub = {0.65, 1.0, 4.0}, statement by statement, on the
 * header's helpers. `widened` evaluates the two products of ADR-1476 in
 * double; otherwise they are upstream's float products. */
static float replay(CiedeLab color_1, CiedeLab color_2, bool widened)
{
    const float ksub_l = (float)0.65;
    const float delta_l_prime = color_2.l - color_1.l;
    const float l_bar = (color_1.l + color_2.l) / 2.0f;
    const float c1 = chroma_of(color_1.a, color_1.b);
    const float c2 = chroma_of(color_2.a, color_2.b);
    const float c_bar = (c1 + c2) / 2.0f;
    const float a_prime_1 = a_prime_of(color_1.a, c_bar);
    const float a_prime_2 = a_prime_of(color_2.a, c_bar);
    const float c_prime_1 = chroma_of(a_prime_1, color_1.b);
    const float c_prime_2 = chroma_of(a_prime_2, color_2.b);
    const float c_bar_prime = (c_prime_1 + c_prime_2) / 2.0f;
    const float delta_c_prime = c_prime_2 - c_prime_1;
    const double l_sq = ciede_sq(l_bar - 50.0f);
    const float s_sub_l = (float)(1. + ((0.015 * l_sq) / sqrt(20.0 + l_sq)));
    const float s_sub_c = (float)(1. + 0.045 * (double)c_bar_prime);
    const float h_prime_1 = ciede_h_prime(color_1.b, a_prime_1);
    const float h_prime_2 = ciede_h_prime(color_2.b, a_prime_2);
    const float delta_h_prime = ciede_delta_h_prime(c1, c2, h_prime_1, h_prime_2);
    const double chroma_product =
        widened ? (double)c_prime_1 * (double)c_prime_2 : (double)(c_prime_1 * c_prime_2);
    const float delta_upcase_h_prime =
        (float)(2.0 * sqrt(chroma_product) * sin((double)delta_h_prime / 2.0));
    const float upcase_h_bar_prime = ciede_upcase_h_bar_prime(h_prime_1, h_prime_2);
    const float upcase_t = ciede_upcase_t(upcase_h_bar_prime);
    const float s_sub_upcase_h = (float)(1.0 + 0.015 * (double)c_bar_prime * (double)upcase_t);
    const float r_sub_t = ciede_r_sub_t(c_bar_prime, upcase_h_bar_prime);
    const float lightness = delta_l_prime / (ksub_l * s_sub_l);
    const float chroma = delta_c_prime / s_sub_c;
    const float hue = delta_upcase_h_prime / (4.0f * s_sub_upcase_h);
    const double rotation =
        widened ? (double)r_sub_t * (double)chroma * (double)hue : (double)(r_sub_t * chroma * hue);

    return (float)sqrt(ciede_sq(lightness) + ciede_sq(chroma) + ciede_sq(hue) + rotation);
}

typedef struct Tally {
    unsigned pairs;
    unsigned not_upstream; /* ciede_delta_e() differs from the float-product replay */
    unsigned told_apart;   /* the two replays differ */
} Tally;

/* Deterministic colours across the range get_lab_color() produces: lightness
 * 4 to 95, a and b within +-70. */
static CiedeLab grid_color(unsigned index, unsigned salt)
{
    unsigned x = index * 2654435761u + salt * 40503u;
    x ^= x >> 15;
    x *= 2246822519u;
    x ^= x >> 13;
    CiedeLab color;
    color.l = 4.0f + (float)(x & 0xffu) * (91.0f / 255.0f);
    color.a = -70.0f + (float)((x >> 8) & 0x3ffu) * (140.0f / 1023.0f);
    color.b = -70.0f + (float)((x >> 18) & 0x3ffu) * (140.0f / 1023.0f);
    return color;
}

/* The second colour of a pair: a few units from the first, as a distorted
 * pixel is from its reference. */
static CiedeLab nearby_color(CiedeLab base, unsigned index)
{
    const CiedeLab offset = grid_color(index, 7u);
    CiedeLab color;
    color.l = base.l + (offset.l - 50.0f) * 0.1f;
    color.a = base.a + offset.a * 0.08f;
    color.b = base.b + offset.b * 0.08f;
    return color;
}

static Tally tally_grid(unsigned count)
{
    Tally tally = {0u, 0u, 0u};
    for (unsigned i = 0u; i < count; i++) {
        const CiedeLab color_1 = grid_color(i, 1u);
        const CiedeLab color_2 = nearby_color(color_1, i);
        const float upstream = replay(color_1, color_2, false);
        const float widened = replay(color_1, color_2, true);
        const float header = ciede_delta_e(color_1, color_2);
        tally.pairs++;
        if (header != upstream)
            tally.not_upstream++;
        if (widened != upstream)
            tally.told_apart++;
    }
    return tally;
}

static char *test_products_are_upstreams_float_products(void)
{
    const Tally tally = tally_grid(20000u);
    if (tally.not_upstream != 0u || tally.told_apart == 0u) {
        (void)fprintf(stderr, "\n%u pairs: %u not upstream's value, %u tell the forms apart\n",
                      tally.pairs, tally.not_upstream, tally.told_apart);
    }
    mu_assert("the grid must tell float products from widened ones", tally.told_apart > 0u);
    mu_assert("ciede_delta_e() must form upstream's float products (ADR-1476)",
              tally.not_upstream == 0u);
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_products_are_upstreams_float_products);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
