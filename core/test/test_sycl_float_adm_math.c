/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * ADR-1434 — what float_adm_sycl.cpp's kernels run per work-item
 * (feature/sycl/sycl_float_adm_math.h) against the CPU extractor's own
 * routines, bit for bit, on the host and in kernels on the default GPU.
 *
 *   - The three expressions adm_tools.c evaluates in fp64 (the 1/30 product
 *     of adm_csf_s(), the centre tap of adm_cm_thresh3x3_s() and the
 *     enhancement-gain clamp of the decouple) against the same C expressions
 *     compiled here, over samples of every magnitude, subnormals and the
 *     neighbourhood of the clamp included. The device has no fp64 type
 *     (ADR-0220): it evaluates them as exact fp32 pairs and replays the fp64
 *     operations in integers where a pair does not decide the rounding. Both
 *     ways are checked, the replay also on its own.
 *   - One scale past the DWT, composed as the kernels compose it (decouple,
 *     terms, row sums; then the host's fold and pooling), against
 *     adm_decouple_s(), adm_csf_s(), adm_csf_den_scale_s() and adm_cm_s(), on
 *     bands whose reduced region does and does not reach the band's edges.
 *
 * A quotient formed with a reciprocal, an fp32 gain, fp32 1/30 and 1/15
 * constants, a threshold summed in another order, cos^2 * (|o|^2 * |t|^2) and
 * a sum per sub-group each fail at least one of these, which is how the twin
 * differed from the CPU before ADR-1434. The reference's quotient is the IEEE
 * fp32 one (ADR-1442); the device check is what shows that the kernel's `/`
 * is that quotient too.
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

#include "feature/adm_float_reference.h"
#include "feature/adm_options.h"
#include "feature/adm_tools.h"
#include "sycl_float_adm_math_probe.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy proposes `nullptr`, but the required MSVC C build does
 * not provide that keyword. Preserve the portable C spelling. ADR-1138. */

#define MAX_W 136
#define MAX_H 80
#define BAND_FLOATS ((size_t)MAX_W * MAX_H)
#define TERM_SLOTS 9u
#define SPOT_SAMPLES 3000000u
#define DEVICE_SPOT_SAMPLES 1000000u
#define DECOUPLE_TRIALS 64

/* adm_tools.c's constants: double literals, so not the fp32 values of those
 * names. test_sycl_float_adm_exact_contract.py holds them to the source. */
#define REFERENCE_ONE_BY_30 0.0333333351
#define REFERENCE_ONE_BY_15 0.0666666701

/* Deterministic generator: the same inputs on every host. */
static uint64_t rng_state = 0x9E3779B97F4A7C15ull;

static uint64_t rng_next(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

static uint32_t bits_of(float x)
{
    uint32_t bits;
    memcpy(&bits, &x, sizeof(bits));
    return bits;
}

static float float_of(uint32_t bits)
{
    float x;
    memcpy(&x, &bits, sizeof(x));
    return x;
}

static bool same_float(float a, float b)
{
    return bits_of(a) == bits_of(b) || (isnan(a) && isnan(b));
}

/* Uniform in [-1, 1). */
static float rng_unit(void)
{
    return (float)((double)(rng_next() >> 40) / 8388608.0 - 1.0);
}

/* A non-negative float of assorted magnitude. */
static float pick_magnitude(void)
{
    switch (rng_next() % 6u) {
    case 0u: /* any finite value */
        return float_of((uint32_t)(rng_next() & 0x7f7fffffu));
    case 1u: /* band magnitudes */
        return (float)(rng_next() % 100000u) * (1.0f / 64.0f);
    case 2u: /* subnormal */
        return float_of((uint32_t)(rng_next() & 0x007fffffu));
    case 3u: /* mid range */
        return (float)(rng_next() >> 40) * 0x1p-12f;
    case 4u: /* next to one */
        return float_of(0x3f800000u + (uint32_t)(rng_next() % 64u));
    default: /* small or zero */
        return (rng_next() & 7u) ? (float)(rng_next() >> 44) * 0x1p-30f : 0.0f;
    }
}

/* adm_csf_s(): `flt = FLOAT_ONE_BY_30 * fabsf(dst)`. */
static float reference_flt(float csf)
{
    return (float)(REFERENCE_ONE_BY_30 * fabsf(csf));
}

/* adm_cm_thresh3x3_s(): `sum += FLOAT_ONE_BY_15 * fabsf(centre)`. */
static float reference_centre(float sum, float centre)
{
    float s = sum;
    s += REFERENCE_ONE_BY_15 * fabsf(centre);
    return s;
}

/* adm_decouple_band_s()'s clamp, with its MIN / MAX spelled out. */
static float reference_gain(float rst, float t, double limit)
{
    if (rst > 0.0f) {
        const double gained = rst * limit;
        rst = (float)(gained < t ? gained : t);
    }
    if (rst < 0.0f) {
        const double gained = rst * limit;
        rst = (float)(gained > t ? gained : t);
    }
    return rst;
}

/* Inputs and the two sides' outputs of the fp64 expressions. */
typedef struct Spots {
    float *in[4];  /* a, sum, rst, t */
    float *out[3]; /* flt, centre, gain */
    size_t n;
} Spots;

static int spots_alloc(Spots *s, size_t n)
{
    memset(s, 0, sizeof(*s));
    s->n = n;
    bool ok = true;
    for (int i = 0; i < 4; i++) {
        s->in[i] = calloc(n, sizeof(float));
        ok = ok && s->in[i];
    }
    for (int i = 0; i < 3; i++) {
        s->out[i] = calloc(n, sizeof(float));
        ok = ok && s->out[i];
    }
    return ok ? 0 : -1;
}

static void spots_free(Spots *s)
{
    for (int i = 0; i < 4; i++)
        free(s->in[i]);
    for (int i = 0; i < 3; i++)
        free(s->out[i]);
}

/* Samples of every magnitude and sign; a quarter of the clamp's inputs sit at
 * the gained product or one fp32 step either side of it. */
static void spots_fill(Spots *s, double limit)
{
    for (size_t i = 0; i < s->n; i++) {
        const float sign = (rng_next() & 1u) ? -1.0f : 1.0f;
        s->in[0][i] = sign * pick_magnitude();
        s->in[1][i] = pick_magnitude();
        float rst = pick_magnitude();
        float t = pick_magnitude();
        const float at_limit = (float)((double)rst * limit);
        const unsigned mode = (unsigned)(rng_next() % 8u);
        if (mode == 0u) {
            t = at_limit;
        } else if (mode == 1u) {
            t = nextafterf(at_limit, INFINITY);
        } else if (mode == 2u) {
            t = nextafterf(at_limit, 0.0f);
        } else if (mode == 3u) {
            t = -t;
        }
        if (rng_next() & 1u) {
            rst = -rst;
            t = -t;
        }
        s->in[2][i] = (rst == 0.0f) ? 1.0f : rst;
        s->in[3][i] = t;
    }
}

static VmafTestFadmSpots spots_view(const Spots *s, double limit)
{
    const VmafTestFadmSpots v = {.a = s->in[0],
                                 .sum = s->in[1],
                                 .rst = s->in[2],
                                 .t = s->in[3],
                                 .n = s->n,
                                 .gain_limit = limit,
                                 .flt = s->out[0],
                                 .centre = s->out[1],
                                 .gain = s->out[2]};
    return v;
}

/* Every output of `s` against the reference expressions. */
static char *spots_check(const Spots *s, double limit, const char *where)
{
    size_t wrong = 0u;
    for (size_t i = 0; i < s->n; i++) {
        const float a = s->in[0][i];
        const float want[3] = {reference_flt(a), reference_centre(s->in[1][i], a),
                               reference_gain(s->in[2][i], s->in[3][i], limit)};
        for (int k = 0; k < 3; k++) {
            if (same_float(want[k], s->out[k][i]))
                continue;
            if (wrong++ < 4u) {
                (void)fprintf(stderr,
                              "\n%s expression %d sample %zu (a=%a sum=%a rst=%a t=%a limit=%g): "
                              "reference %a, twin %a\n",
                              where, k, i, (double)a, (double)s->in[1][i], (double)s->in[2][i],
                              (double)s->in[3][i], limit, (double)want[k], (double)s->out[k][i]);
            }
        }
    }
    mu_assert("an fp64 expression of the reference is not reproduced", wrong == 0u);
    return NULL;
}

/* 100 and 1 are fp32 values, where the clamp's fp64 product is exact; the
 * others are not, and reach the replay. */
static const double GAIN_LIMITS[5] = {100.0, 1.0, 1.2, 37.3, 99.999};

static char *test_constants_are_the_reference_literals(void)
{
    double c[4];
    vmaf_test_sycl_fadm_constants(c);
    mu_assert("kOneBy30 must be the double literal FLOAT_ONE_BY_30", c[0] == REFERENCE_ONE_BY_30);
    mu_assert("kOneBy15 must be the double literal FLOAT_ONE_BY_15", c[1] == REFERENCE_ONE_BY_15);
    mu_assert("kOneBy30's pair must be the literal to 2^-48",
              fabs(c[2] - REFERENCE_ONE_BY_30) <= REFERENCE_ONE_BY_30 * 0x1p-48);
    mu_assert("kOneBy15's pair must be the literal to 2^-48",
              fabs(c[3] - REFERENCE_ONE_BY_15) <= REFERENCE_ONE_BY_15 * 0x1p-48);
    return NULL;
}

static char *check_spots_host(Spots *s, double limit, bool replay_only, const char *where)
{
    const VmafTestFadmSpots view = spots_view(s, limit);
    size_t undecided = 0u;
    vmaf_test_sycl_fadm_spots_host(&view, replay_only ? 1 : 0, &undecided);
    char *msg = spots_check(s, limit, where);
    if (msg)
        return msg;
    /* The pair decides most samples and not all: both ways are exercised. */
    mu_assert("no sample reached the replay", undecided > 0u);
    mu_assert("no sample was decided by the pair", undecided < s->n);
    return NULL;
}

static char *test_fp64_expressions_host(void)
{
    Spots s;
    mu_assert("allocation failed", spots_alloc(&s, SPOT_SAMPLES) == 0);
    char *msg = NULL;
    for (int l = 0; l < 5 && !msg; l++) {
        spots_fill(&s, GAIN_LIMITS[l]);
        msg = check_spots_host(&s, GAIN_LIMITS[l], false, "host");
        if (!msg)
            msg = check_spots_host(&s, GAIN_LIMITS[l], true, "host replay");
    }
    spots_free(&s);
    return msg;
}

/* Set when a device entry point found no GPU. */
static bool no_device = false;

static char *test_fp64_expressions_device(void)
{
    Spots s;
    mu_assert("allocation failed", spots_alloc(&s, DEVICE_SPOT_SAMPLES) == 0);
    char *msg = NULL;
    for (int l = 0; l < 5 && !msg && !no_device; l++) {
        spots_fill(&s, GAIN_LIMITS[l]);
        const VmafTestFadmSpots view = spots_view(&s, GAIN_LIMITS[l]);
        const int err = vmaf_test_sycl_fadm_spots_device(&view);
        if (err == -ENODEV) {
            no_device = true;
        } else if (err) {
            msg = "the device run of the fp64 expressions failed";
        } else {
            msg = spots_check(&s, GAIN_LIMITS[l], "device");
        }
    }
    spots_free(&s);
    return msg;
}

/* One scale's buffers in the kernels' layout, and the reference's results. */
typedef struct Scale {
    float *ref;    /* 4 sub-bands: a, h, v, d */
    float *dis;    /* 4 sub-bands */
    float *csf[4]; /* the twin's csf_a, csf_fa, csf_r, csf_fr */
    float *cpu[6]; /* the reference's decouple_r, decouple_a, csf_a, csf_fa, csf_r, csf_fr */
    float *rows;
} Scale;

static int scale_alloc(Scale *s)
{
    memset(s, 0, sizeof(*s));
    s->ref = calloc(4u * BAND_FLOATS, sizeof(float));
    s->dis = calloc(4u * BAND_FLOATS, sizeof(float));
    s->rows = calloc((size_t)TERM_SLOTS * MAX_H, sizeof(float));
    bool ok = s->ref && s->dis && s->rows;
    for (int i = 0; i < 4; i++) {
        s->csf[i] = calloc(3u * BAND_FLOATS, sizeof(float));
        ok = ok && s->csf[i];
    }
    for (int i = 0; i < 6; i++) {
        s->cpu[i] = calloc(3u * BAND_FLOATS, sizeof(float));
        ok = ok && s->cpu[i];
    }
    return ok ? 0 : -1;
}

static void scale_free(Scale *s)
{
    free(s->ref);
    free(s->dis);
    free(s->rows);
    for (int i = 0; i < 4; i++)
        free(s->csf[i]);
    for (int i = 0; i < 6; i++)
        free(s->cpu[i]);
}

/* The reference's (h, v, d) view of a three-sub-band buffer. */
static adm_dwt_band_t_s view3(float *buf, int w, int h)
{
    const size_t plane = (size_t)w * (size_t)h;
    const adm_dwt_band_t_s b = {
        .band_a = NULL, .band_v = buf + plane, .band_h = buf, .band_d = buf + 2u * plane};
    return b;
}

/* The reference's view of the (h, v, d) sub-bands of a DWT band buffer. */
static adm_dwt_band_t_s view4(float *buf, int w, int h)
{
    return view3(buf + (size_t)w * (size_t)h, w, h);
}

/* Band content that reaches every branch of the decouple: mostly a distorted
 * band correlated with the reference, with runs of aligned, enhanced, opposed
 * and zero samples, and whole samples whose (h, v) vectors are one degree
 * apart, where the outcome of the angle test depends on the association of
 * cos^2 * |o|^2 * |t|^2. */
static void fill_bands(Scale *s, int w, int h, float amplitude)
{
    const size_t plane = (size_t)w * (size_t)h;
    for (size_t i = 0; i < 4u * plane; i++) {
        const float o = rng_unit() * amplitude;
        float t = o * (0.5f + 0.75f * (rng_unit() + 1.0f)) + rng_unit() * amplitude * 0.05f;
        const unsigned kind = (unsigned)(rng_next() % 16u);
        if (kind == 0u) {
            t = o;
        } else if (kind == 1u) {
            t = o * 1.5f;
        } else if (kind == 2u) {
            t = o * 1.1f;
        } else if (kind == 3u) {
            t = -o;
        } else if (kind == 4u) {
            t = 0.0f;
        }
        s->ref[i] = (kind == 5u) ? 0.0f : o;
        s->dis[i] = t;
    }
    for (size_t i = 0; i < plane; i += 3u) {
        const float gain = 1.0f + 0.4f * (rng_unit() + 1.0f);
        s->dis[plane + i] = s->ref[plane + i] * gain;
        s->dis[2u * plane + i] = s->ref[2u * plane + i] * gain;
        s->dis[3u * plane + i] = s->ref[3u * plane + i] * gain;
    }
    const double cos_1deg = 0.99984769515639127;
    const double sin_1deg = 0.017452406437283512;
    for (size_t i = 1; i < plane; i += 3u) {
        const double oh = s->ref[plane + i];
        const double ov = s->ref[2u * plane + i];
        const double gain = 1.0 + 0.4 * ((double)rng_unit() + 1.0);
        s->dis[plane + i] = (float)(gain * (cos_1deg * oh - sin_1deg * ov));
        s->dis[2u * plane + i] = (float)(gain * (sin_1deg * oh + cos_1deg * ov));
    }
}

typedef struct Options {
    double gain_limit;
    double noise_weight;
    double p_norm;
    int bypass_cm;
    int scale;
} Options;

/* adm_csf_s() with the options the twin passes. */
static void reference_csf(const adm_dwt_band_t_s *src, const adm_dwt_band_t_s *dst,
                          const adm_dwt_band_t_s *flt, int w, int h, const Options *o)
{
    const int stride = w * (int)sizeof(float);
    adm_csf_s(src, dst, flt, h, o->scale, w, h, stride, stride, ADM_BORDER_FACTOR,
              DEFAULT_ADM_NORM_VIEW_DIST, DEFAULT_ADM_REF_DISPLAY_HEIGHT, DEFAULT_ADM_CSF_MODE,
              DEFAULT_ADM_CSF_LUMINANCE_LEVEL, DEFAULT_ADM_CSF_SCALE, DEFAULT_ADM_CSF_DIAG_SCALE,
              -1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0);
}

/* adm_cm_s() with the options the twin passes. */
static float reference_cm(const adm_dwt_band_t_s *src, const adm_dwt_band_t_s *csf_f,
                          const adm_dwt_band_t_s *csf_a, int w, int h, const Options *o,
                          double noise_weight)
{
    const int stride = w * (int)sizeof(float);
    return adm_cm_s(src, csf_f, csf_a, w, h, stride, stride, stride, ADM_BORDER_FACTOR, o->scale,
                    DEFAULT_ADM_NORM_VIEW_DIST, DEFAULT_ADM_REF_DISPLAY_HEIGHT,
                    DEFAULT_ADM_CSF_MODE, DEFAULT_ADM_CSF_LUMINANCE_LEVEL, DEFAULT_ADM_CSF_SCALE,
                    DEFAULT_ADM_CSF_DIAG_SCALE, noise_weight, o->bypass_cm, o->p_norm, -1.0, -1.0,
                    -1.0, -1.0, -1.0, -1.0, -1.0, -1.0);
}

/* compute_adm()'s scale body past the DWT: den_scale, num_scale and
 * aim_num_scale in out[0..2]. Leaves decouple and CSF results in s->cpu[]. */
static void reference_scale(Scale *s, int w, int h, const Options *o, float out[3])
{
    const int stride = w * (int)sizeof(float);
    const adm_dwt_band_t_s ref = view4(s->ref, w, h);
    const adm_dwt_band_t_s dis = view4(s->dis, w, h);
    const adm_dwt_band_t_s r = view3(s->cpu[0], w, h);
    const adm_dwt_band_t_s a = view3(s->cpu[1], w, h);
    const adm_dwt_band_t_s csf_a = view3(s->cpu[2], w, h);
    const adm_dwt_band_t_s csf_fa = view3(s->cpu[3], w, h);
    const adm_dwt_band_t_s csf_r = view3(s->cpu[4], w, h);
    const adm_dwt_band_t_s csf_fr = view3(s->cpu[5], w, h);

    adm_decouple_s(&ref, &dis, &r, &a, w, h, stride, stride, stride, stride, ADM_BORDER_FACTOR,
                   o->gain_limit);
    out[0] = adm_csf_den_scale_s(&ref, h, o->scale, w, h, stride, ADM_BORDER_FACTOR,
                                 DEFAULT_ADM_NORM_VIEW_DIST, DEFAULT_ADM_REF_DISPLAY_HEIGHT,
                                 DEFAULT_ADM_CSF_MODE, DEFAULT_ADM_CSF_LUMINANCE_LEVEL,
                                 DEFAULT_ADM_CSF_SCALE, DEFAULT_ADM_CSF_DIAG_SCALE, o->noise_weight,
                                 o->p_norm, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0);
    reference_csf(&a, &csf_a, &csf_fa, w, h, o);
    out[1] = reference_cm(&r, &csf_fa, &csf_a, w, h, o, o->noise_weight);
    reference_csf(&r, &csf_r, &csf_fr, w, h, o);
    out[2] = reference_cm(&a, &csf_fr, &csf_r, w, h, o, 0.0);
}

/* The three kernels over the scale, then the host's fold and pooling:
 * den_scale, num_scale and aim_num_scale in out[0..2]. Returns the probe's
 * status. */
static int twin_scale(Scale *s, int w, int h, const Options *o, bool on_device, float out[3])
{
    const AdmBorderS r = adm_border_s(w, h, ADM_BORDER_FACTOR);
    VmafTestFadmScale in = {.ref = s->ref,
                            .dis = s->dis,
                            .w = w,
                            .h = h,
                            .cos_1deg_sq = adm_decouple_cos_1deg_sq_s(),
                            .gain_limit = o->gain_limit,
                            .p_norm = o->p_norm,
                            .bypass_cm = o->bypass_cm,
                            .left = r.left,
                            .top = r.top,
                            .right = r.right,
                            .bottom = r.bottom,
                            .csf = {s->csf[0], s->csf[1], s->csf[2], s->csf[3]},
                            .rows = s->rows};
    adm_csf_rfactor_s(o->scale, DEFAULT_ADM_NORM_VIEW_DIST, DEFAULT_ADM_REF_DISPLAY_HEIGHT,
                      DEFAULT_ADM_CSF_MODE, DEFAULT_ADM_CSF_LUMINANCE_LEVEL, DEFAULT_ADM_CSF_SCALE,
                      DEFAULT_ADM_CSF_DIAG_SCALE, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0,
                      in.rfactor);
    const int err = vmaf_test_sycl_fadm_scale(&in, on_device ? 1 : 0);
    if (err)
        return err;
    const int region_w = r.right - r.left;
    const int region_h = r.bottom - r.top;
    float accum[TERM_SLOTS];
    for (unsigned slot = 0u; slot < TERM_SLOTS; slot++) {
        /* adm_fold3_s(): the rows of one slot, top to bottom, in fp32. */
        float sum = 0.0f;
        for (int y = 0; y < region_h; y++)
            sum += s->rows[(size_t)slot * (size_t)region_h + (size_t)y];
        accum[slot] = sum;
    }
    out[0] = adm_pool_bands_s(accum + 0, region_w, region_h, o->noise_weight, o->p_norm);
    out[1] = adm_pool_bands_s(accum + 3, region_w, region_h, o->noise_weight, o->p_norm);
    out[2] = adm_pool_bands_s(accum + 6, region_w, region_h, 0.0, o->p_norm);
    return 0;
}

/* Every sample of the twin's four CSF buffers against the reference's. */
static char *compare_csf(const Scale *s, int w, int h, const char *where)
{
    const size_t count = 3u * (size_t)w * (size_t)h;
    for (int k = 0; k < 4; k++) {
        for (size_t i = 0; i < count; i++) {
            const uint32_t want = bits_of(s->cpu[2 + k][i]);
            const uint32_t got = bits_of(s->csf[k][i]);
            if (want != got) {
                (void)fprintf(stderr,
                              "\n%s decouple+csf buffer %d sample %zu: cpu=%08x twin=%08x\n", where,
                              k, i, want, got);
            }
            mu_assert("decouple + CSF must equal adm_decouple_s() + adm_csf_s()", want == got);
        }
    }
    return NULL;
}

/* Bands below 25 samples a side: the reference's decouple and CSF then cover
 * the whole band, so every sample can be compared. */
static char *check_decouple(Scale *s, bool on_device, const char *where)
{
    static const double gains[4] = {100.0, 1.2, 1.0, 3.7};
    static const float amplitudes[4] = {1.0f, 37.5f, 4000.0f, 1e-3f};
    for (int trial = 0; trial < DECOUPLE_TRIALS; trial++) {
        const int w = 2 + (int)(rng_next() % 23u);
        const int h = 2 + (int)(rng_next() % 23u);
        const Options o = {.gain_limit = gains[trial % 4],
                           .noise_weight = DEFAULT_ADM_NOISE_WEIGHT,
                           .p_norm = 3.0,
                           .bypass_cm = 0,
                           .scale = trial % 4};
        float want[3];
        float got[3];
        fill_bands(s, w, h, amplitudes[(trial / 4) % 4]);
        reference_scale(s, w, h, &o, want);
        const int err = twin_scale(s, w, h, &o, on_device, got);
        if (err == -ENODEV) {
            no_device = true;
            return NULL;
        }
        mu_assert("the twin's kernels failed", err == 0);
        char *msg = compare_csf(s, w, h, where);
        if (msg)
            return msg;
    }
    return NULL;
}

/* One band size and option set: the three pooled values of the twin against
 * the reference's. */
static char *check_reduction_case(Scale *s, int w, int h, const Options *o, bool on_device,
                                  const char *where)
{
    static const char *const names[3] = {"den_scale", "num_scale", "aim_num_scale"};
    float want[3];
    float got[3];
    reference_scale(s, w, h, o, want);
    const int err = twin_scale(s, w, h, o, on_device, got);
    if (err == -ENODEV) {
        no_device = true;
        return NULL;
    }
    mu_assert("the twin's kernels failed", err == 0);
    for (int k = 0; k < 3; k++) {
        if (bits_of(want[k]) != bits_of(got[k])) {
            (void)fprintf(stderr, "\n%s %s %dx%d p=%g bypass=%d: cpu=%.9g twin=%.9g\n", where,
                          names[k], w, h, o->p_norm, o->bypass_cm, (double)want[k], (double)got[k]);
        }
        mu_assert("a scale's reductions must equal the reference's",
                  bits_of(want[k]) == bits_of(got[k]));
    }
    return NULL;
}

static char *check_reductions(Scale *s, bool on_device, const char *where)
{
    /* Bands of 14 samples or fewer keep their edges in the reduced region,
     * so the mirrored and clamped threshold taps are part of the sums. */
    static const int dims[8][2] = {{2, 2},   {5, 9},   {9, 5},    {14, 14},
                                   {15, 31}, {67, 43}, {136, 80}, {81, 21}};
    static const Options variants[4] = {
        {.gain_limit = 100.0, .noise_weight = DEFAULT_ADM_NOISE_WEIGHT, .p_norm = 3.0},
        {.gain_limit = 1.2, .noise_weight = DEFAULT_ADM_NOISE_WEIGHT, .p_norm = 3.0, .scale = 1},
        {.gain_limit = 100.0, .noise_weight = 0.0, .p_norm = 3.0, .scale = 2},
        {.gain_limit = 100.0, .noise_weight = 0.5, .p_norm = 3.0, .bypass_cm = 1, .scale = 3},
    };
    for (int d = 0; d < 8 && !no_device; d++) {
        for (int v = 0; v < 4 && !no_device; v++) {
            fill_bands(s, dims[d][0], dims[d][1], (v == 1) ? 250.0f : 12.0f);
            char *msg =
                check_reduction_case(s, dims[d][0], dims[d][1], &variants[v], on_device, where);
            if (msg)
                return msg;
        }
    }
    return NULL;
}

static char *with_scale(char *(*check)(Scale *, bool, const char *), bool on_device,
                        const char *where)
{
    Scale s;
    mu_assert("allocation failed", scale_alloc(&s) == 0);
    char *msg = check(&s, on_device, where);
    scale_free(&s);
    return msg;
}

static char *test_decouple_csf_host(void)
{
    return with_scale(check_decouple, false, "host");
}

static char *test_scale_reductions_host(void)
{
    return with_scale(check_reductions, false, "host");
}

static char *test_decouple_csf_device(void)
{
    return no_device ? NULL : with_scale(check_decouple, true, "device");
}

static char *test_scale_reductions_device(void)
{
    return no_device ? NULL : with_scale(check_reductions, true, "device");
}

static char *run_host_tests(void)
{
    mu_run_test(test_constants_are_the_reference_literals);
    mu_run_test(test_fp64_expressions_host);
    mu_run_test(test_decouple_csf_host);
    mu_run_test(test_scale_reductions_host);
    return NULL;
}

static char *run_device_tests(void)
{
    mu_run_test(test_fp64_expressions_device);
    mu_run_test(test_decouple_csf_device);
    mu_run_test(test_scale_reductions_device);
    return NULL;
}

char *run_tests(void)
{
    mu_assert_msg(run_host_tests());
    mu_assert_msg(run_device_tests());
    if (no_device) {
        (void)fprintf(stderr, "[skip: no SYCL GPU for the device half] ");
        mu_skipped = 1;
    }
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
