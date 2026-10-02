/**
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: EUPL-1.2
 *
 *  The x86 kernels of the float ADM pipeline return the scalar functions'
 *  bits (ADR-1473).
 *
 *    float_adm_dwt2_avx2() / float_adm_dwt2_avx512()   against adm_dwt2_s()
 *    float_adm_csf_avx2()  / float_adm_csf_avx512()    against adm_csf_plane_s()
 *
 *  Every output buffer is compared byte for byte, stride padding included
 *  (both sides start from the same poison pattern, so a store outside the
 *  plane shows as well).
 *
 *    1. positive: picture data, fractional data and 16-bit data, on sizes
 *       around the vector widths and down to the 17x17 minimum;
 *    2. boundary: frames of signed zeros. The scalar wavelet starts each
 *       four-tap sum at +0, so a sum of negative zeros is +0; a kernel that
 *       starts at the first product returns -0 (the kernels did until they
 *       were wired);
 *    3. negative: infinities, NaN and denormals travel through both sides
 *       alike, and a factor that makes the CSF product overflow does too;
 *    4. end to end: compute_adm() returns the same doubles with the SIMD
 *       kernels masked off, with AVX2 and with AVX-512.
 */

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "test.h"

#include "config.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy also proposes the `nullptr` keyword, but MSVC's
 * documented /std:clatest C23 feature set does not include `nullptr` while the
 * required Windows build compiles this TU with cl.exe, and this file mirrors
 * the C spelling of the surface it exercises. ADR-1138. */

#if ARCH_X86

#include "cpu.h"
#include "feature/adm.h"
#include "feature/adm_options.h"
#include "feature/adm_tools.h"
#include "feature/x86/float_adm_avx2.h"
#if HAVE_AVX512
#include "feature/x86/float_adm_avx512.h"
#endif
#include "mem.h"
#include "x86/cpu.h"

#define POISON_BITS (0x7FC0DEADu)
#define MAX_DIM (600)
#define NUM_INPUT_CLASSES (6)
#define SPECIAL_VALUES (4) /* index of the class with NaN, infinities, denormals */

typedef int (*Dwt2Fn)(const float *src, const adm_dwt_band_t_s *dst, int **ind_y, int **ind_x,
                      int w, int h, int src_stride, int dst_stride);

static uint32_t next_u32(uint32_t *seed)
{
    uint32_t s = *seed;
    s ^= s << 13u;
    s ^= s >> 17u;
    s ^= s << 5u;
    *seed = s;
    return s;
}

static float from_bits(uint32_t bits)
{
    float f;
    (void)memcpy(&f, &bits, sizeof(f));
    return f;
}

/* One sample of input class `cls`. */
static float sample(int cls, uint32_t *seed)
{
    static const uint32_t SPECIAL[8] = {
        0x7F800000u, /* +inf */
        0xFF800000u, /* -inf */
        0x7FC00000u, /* quiet NaN */
        0x00000001u, /* smallest denormal */
        0x807FFFFFu, /* largest negative denormal */
        0x7F7FFFFFu, /* FLT_MAX */
        0x80000000u, /* -0 */
        0x3F800000u, /* 1 */
    };
    const uint32_t r = next_u32(seed);
    switch (cls) {
    case 0: /* 8-bit picture, centred as float_adm centres it */
        return (float)(r % 256u) - 128.0f;
    case 1: /* fractional, both signs */
        return ((float)(r % 2000001u) - 1000000.0f) / 977.0f;
    case 2: /* zeros of both signs only */
        return (r & 1u) ? -0.0f : 0.0f;
    case 3: /* zeros of both signs among values */
        return (r & 3u) ? ((r & 4u) ? -0.0f : 0.0f) : (float)(r % 1024u) / 4.0f;
    case SPECIAL_VALUES:
        return (r & 7u) ? (float)(r % 4096u) / 16.0f - 128.0f : from_bits(SPECIAL[(r >> 3u) & 7u]);
    default: /* 16-bit picture */
        return (float)(r % 65536u) / 256.0f - 128.0f;
    }
}

/* Which NaN an operation returns when two different NaNs meet is the first
 * operand's, and a compiler may commute the operands of an addition, so the
 * scalar and the vector code can return NaNs that differ in sign or payload.
 * A NaN anywhere fails the frame (ADR-1302), so for the special-value class
 * every NaN is replaced by one pattern before the comparison: "NaN where the
 * reference has NaN" is compared, its payload is not. */
static void canonical_nans(float *p, size_t n)
{
    const float nan = from_bits(0x7FC00000u);
    for (size_t k = 0; k < n; k++) {
        if (isnan(p[k])) {
            p[k] = nan;
        }
    }
}

static void fill_poison(float *p, size_t n)
{
    const float poison = from_bits(POISON_BITS);
    for (size_t k = 0; k < n; k++) {
        p[k] = poison;
    }
}

/* The buffers of one wavelet comparison; zeroed so a partial allocation is
 * safe to release. */
typedef struct Dwt2Case {
    float *src;
    float *ref_bands;
    float *simd_bands;
    int *index[8];
    size_t band_floats;
} Dwt2Case;

static void dwt2_case_free(Dwt2Case *c)
{
    aligned_free(c->src);
    aligned_free(c->ref_bands);
    aligned_free(c->simd_bands);
    for (int k = 0; k < 8; k++) {
        free(c->index[k]);
    }
}

static bool dwt2_case_alloc(Dwt2Case *c, int h, int src_px_stride, int dst_px_stride)
{
    const size_t half_h = (size_t)((h + 1) / 2);
    (void)memset(c, 0, sizeof(*c));
    c->band_floats = half_h * (size_t)dst_px_stride;
    c->src = aligned_malloc(sizeof(float) * (size_t)src_px_stride * (size_t)h, 64);
    c->ref_bands = aligned_malloc(sizeof(float) * 4u * c->band_floats, 64);
    c->simd_bands = aligned_malloc(sizeof(float) * 4u * c->band_floats, 64);
    bool ok = c->src && c->ref_bands && c->simd_bands;
    for (int k = 0; k < 8; k++) {
        c->index[k] = malloc(sizeof(int) * (size_t)(MAX_DIM / 2 + 2));
        ok = ok && c->index[k];
    }
    return ok;
}

static adm_dwt_band_t_s bands_of(float *base, size_t band_floats)
{
    const adm_dwt_band_t_s b = {.band_a = base,
                                .band_h = base + band_floats,
                                .band_v = base + 2u * band_floats,
                                .band_d = base + 3u * band_floats};
    return b;
}

/* One geometry and input class through adm_dwt2_s() and `kernel`. */
static char *compare_dwt2(Dwt2Fn kernel, int w, int h, int cls, uint32_t *seed)
{
    const int src_px_stride = w + (int)(next_u32(seed) % 7u);
    const int dst_px_stride = (w + 1) / 2 + (int)(next_u32(seed) % 5u);
    Dwt2Case c;
    char *msg = NULL;

    if (!dwt2_case_alloc(&c, h, src_px_stride, dst_px_stride)) {
        dwt2_case_free(&c);
        return "allocation failed";
    }
    for (size_t k = 0; k < (size_t)src_px_stride * (size_t)h; k++) {
        c.src[k] = sample(cls, seed);
    }
    fill_poison(c.ref_bands, 4u * c.band_floats);
    fill_poison(c.simd_bands, 4u * c.band_floats);
    dwt2_src_indices_filt_s(&c.index[0], &c.index[4], w, h);

    const adm_dwt_band_t_s ref = bands_of(c.ref_bands, c.band_floats);
    const adm_dwt_band_t_s simd = bands_of(c.simd_bands, c.band_floats);
    const int src_stride = src_px_stride * (int)sizeof(float);
    const int dst_stride = dst_px_stride * (int)sizeof(float);
    const int ref_err =
        adm_dwt2_s(c.src, &ref, &c.index[0], &c.index[4], w, h, src_stride, dst_stride);
    const int simd_err =
        kernel(c.src, &simd, &c.index[0], &c.index[4], w, h, src_stride, dst_stride);

    if (cls == SPECIAL_VALUES) {
        canonical_nans(c.ref_bands, 4u * c.band_floats);
        canonical_nans(c.simd_bands, 4u * c.band_floats);
    }
    if (ref_err || simd_err) {
        msg = "a wavelet returned an error";
    } else if (memcmp(c.ref_bands, c.simd_bands, sizeof(float) * 4u * c.band_floats) != 0) {
        msg = "a wavelet kernel differs from adm_dwt2_s()";
    }
    dwt2_case_free(&c);
    return msg;
}

/* Widths and heights around the 8- and 16-wide loops of both passes (the
 * horizontal pass works on half the width) and the 17x17 minimum. */
static const int DIMS[] = {17, 18, 19, 20, 23, 31, 32, 33, 34, 35,  36,  47,  48,  49, 50,
                           63, 64, 65, 66, 67, 68, 69, 70, 71, 129, 130, 131, 257, 576};
#define NUM_DIMS ((int)(sizeof(DIMS) / sizeof(DIMS[0])))

/* Input classes cls_first..cls_last at one size. */
static char *sweep_dwt2_classes(Dwt2Fn kernel, int w, int h, int cls_first, int cls_last,
                                uint32_t *seed)
{
    for (int cls = cls_first; cls <= cls_last; cls++) {
        char *msg = compare_dwt2(kernel, w, h, cls, seed);
        if (msg) {
            return msg;
        }
    }
    return NULL;
}

static char *sweep_dwt2(Dwt2Fn kernel, int cls_first, int cls_last)
{
    static const int HEIGHTS[5] = {17, 18, 19, 33, 64};
    uint32_t seed = 0x9E3779B9u;
    for (int wi = 0; wi < NUM_DIMS; wi++) {
        for (int hi = 0; hi < 5; hi++) {
            char *msg =
                sweep_dwt2_classes(kernel, DIMS[wi], HEIGHTS[hi], cls_first, cls_last, &seed);
            if (msg) {
                return msg;
            }
        }
    }
    return NULL;
}

/* The buffers of one CSF comparison. */
typedef struct CsfCase {
    float *src;
    float *out[4]; /* reference dst, reference flt, kernel dst, kernel flt */
    size_t floats;
} CsfCase;

static void csf_case_free(CsfCase *c)
{
    aligned_free(c->src);
    for (int k = 0; k < 4; k++) {
        aligned_free(c->out[k]);
    }
}

/* One plane through adm_csf_plane_s() and `kernel`. */
static char *compare_csf(adm_csf_plane_fn kernel, int w, int h, int cls, float factor,
                         uint32_t *seed)
{
    /* FLOAT_ONE_BY_30 of adm_tools.c: a double constant. */
    const double one_by_30 = 0.0333333351;
    const int src_px_stride = w + (int)(next_u32(seed) % 7u);
    const int dst_px_stride = w + (int)(next_u32(seed) % 5u);
    CsfCase c = {.floats = (size_t)dst_px_stride * (size_t)h};
    bool ok = true;

    c.src = aligned_malloc(sizeof(float) * (size_t)src_px_stride * (size_t)h, 64);
    for (int k = 0; k < 4; k++) {
        c.out[k] = aligned_malloc(sizeof(float) * c.floats, 64);
        ok = ok && c.out[k];
    }
    if (!ok || !c.src) {
        csf_case_free(&c);
        return "allocation failed";
    }
    for (size_t k = 0; k < (size_t)src_px_stride * (size_t)h; k++) {
        c.src[k] = sample(cls, seed);
    }
    for (int k = 0; k < 4; k++) {
        fill_poison(c.out[k], c.floats);
    }
    const int src_stride = src_px_stride * (int)sizeof(float);
    const int dst_stride = dst_px_stride * (int)sizeof(float);
    adm_csf_plane_s(c.src, c.out[0], c.out[1], w, h, src_stride, dst_stride, factor, one_by_30);
    kernel(c.src, c.out[2], c.out[3], w, h, src_stride, dst_stride, factor, one_by_30);

    for (int k = 0; cls == SPECIAL_VALUES && k < 4; k++) {
        canonical_nans(c.out[k], c.floats);
    }
    const bool same = memcmp(c.out[0], c.out[2], sizeof(float) * c.floats) == 0 &&
                      memcmp(c.out[1], c.out[3], sizeof(float) * c.floats) == 0;
    csf_case_free(&c);
    return same ? NULL : "a CSF kernel differs from adm_csf_plane_s()";
}

/* Every weight at one width and input class: weights of the Watson97 and
 * Barten sizes; FLT_MAX / 4 makes the product overflow. */
static char *sweep_csf_factors(adm_csf_plane_fn kernel, int w, int cls, uint32_t *seed)
{
    static const float FACTORS[5] = {0.0173803f, 0.0456674f, 1.21f, 26.98f, 8.5e37f};
    for (int f = 0; f < 5; f++) {
        char *msg = compare_csf(kernel, w, 3, cls, FACTORS[f], seed);
        if (msg) {
            return msg;
        }
    }
    return NULL;
}

/* Every width from 1 to 70 (both sides of the 8- and 16-wide loops) and six
 * input classes. */
static char *sweep_csf(adm_csf_plane_fn kernel)
{
    uint32_t seed = 0x2545F491u;
    for (int w = 1; w <= 70; w++) {
        for (int cls = 0; cls < NUM_INPUT_CLASSES; cls++) {
            char *msg = sweep_csf_factors(kernel, w, cls, &seed);
            if (msg) {
                return msg;
            }
        }
    }
    return NULL;
}

/* positive: ordinary data of three kinds, every size. */
static char *test_dwt2_avx2_matches_scalar_on_picture_data(void)
{
    char *msg = sweep_dwt2(float_adm_dwt2_avx2, 0, 1);
    return msg ? msg : sweep_dwt2(float_adm_dwt2_avx2, 5, 5);
}

/* boundary: zeros of both signs. */
static char *test_dwt2_avx2_matches_scalar_on_signed_zeros(void)
{
    return sweep_dwt2(float_adm_dwt2_avx2, 2, 3);
}

/* negative: NaN, infinities, denormals. */
static char *test_dwt2_avx2_matches_scalar_on_special_values(void)
{
    return sweep_dwt2(float_adm_dwt2_avx2, SPECIAL_VALUES, SPECIAL_VALUES);
}

static char *test_csf_avx2_matches_scalar(void)
{
    return sweep_csf(float_adm_csf_avx2);
}

#if HAVE_AVX512
static char *test_dwt2_avx512_matches_scalar_on_picture_data(void)
{
    char *msg = sweep_dwt2(float_adm_dwt2_avx512, 0, 1);
    return msg ? msg : sweep_dwt2(float_adm_dwt2_avx512, 5, 5);
}

static char *test_dwt2_avx512_matches_scalar_on_signed_zeros(void)
{
    return sweep_dwt2(float_adm_dwt2_avx512, 2, 3);
}

static char *test_dwt2_avx512_matches_scalar_on_special_values(void)
{
    return sweep_dwt2(float_adm_dwt2_avx512, SPECIAL_VALUES, SPECIAL_VALUES);
}

static char *test_csf_avx512_matches_scalar(void)
{
    return sweep_csf(float_adm_csf_avx512);
}
#endif

/* Everything compute_adm() returns: the score, its numerator and
 * denominator, the AIM score, then the eight per-scale values. */
#define ADM_OUT_VALUES (12)

typedef struct AdmOut {
    double v[ADM_OUT_VALUES];
    int err;
} AdmOut;

/* compute_adm() with the default options on one frame pair, with only the
 * instruction sets of `permitted` in use. */
static AdmOut run_compute_adm(const float *ref, const float *dis, int w, int h, int px_stride,
                              unsigned permitted)
{
    AdmOut out;
    const int stride = px_stride * (int)sizeof(float);
    (void)memset(&out, 0, sizeof(out));
    vmaf_set_cpu_flags_mask(permitted);
    out.err = compute_adm(ref, dis, w, h, stride, stride, &out.v[0], &out.v[1], &out.v[2],
                          &out.v[4], ADM_BORDER_FACTOR, DEFAULT_ADM_ENHN_GAIN_LIMIT,
                          DEFAULT_ADM_NORM_VIEW_DIST, DEFAULT_ADM_REF_DISPLAY_HEIGHT,
                          ADM_CSF_MODE_WATSON97, 100.0, 1.0, 1.0, 0.03125, 0, 3.0, &out.v[3], -1.0,
                          -1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0, 0, false, 0u);
    vmaf_set_cpu_flags_mask(~0u);
    return out;
}

/* True when every value of `a` has the bits of the same value of `b`. */
static bool same_bits(const AdmOut *a, const AdmOut *b)
{
    for (int k = 0; k < ADM_OUT_VALUES; k++) {
        uint64_t x;
        uint64_t y;
        (void)memcpy(&x, &a->v[k], sizeof(x));
        (void)memcpy(&y, &b->v[k], sizeof(y));
        if (x != y) {
            return false;
        }
    }
    return true;
}

/* One frame size: the scalar path against AVX2 and against everything the
 * processor has. */
static char *compare_dispatch(int w, int h, uint32_t *seed)
{
    const unsigned none = 0u;
    const unsigned up_to_avx2 = ~(unsigned)VMAF_X86_CPU_FLAG_AVX512;
    const unsigned everything = ~0u;
    const int px_stride = w + 5;
    const size_t n = (size_t)px_stride * (size_t)h;
    float *ref = aligned_malloc(sizeof(float) * n, 64);
    float *dis = aligned_malloc(sizeof(float) * n, 64);
    char *msg = NULL;

    if (!ref || !dis) {
        aligned_free(ref);
        aligned_free(dis);
        return "allocation failed";
    }
    for (size_t k = 0; k < n; k++) {
        ref[k] = sample(0, seed);
        dis[k] = ref[k] + (float)(next_u32(seed) % 41u) - 20.0f;
    }
    const AdmOut scalar = run_compute_adm(ref, dis, w, h, px_stride, none);
    const AdmOut avx2 = run_compute_adm(ref, dis, w, h, px_stride, up_to_avx2);
    const AdmOut native = run_compute_adm(ref, dis, w, h, px_stride, everything);

    if (scalar.err || avx2.err || native.err) {
        msg = "compute_adm failed";
    } else if (!same_bits(&scalar, &avx2)) {
        msg = "compute_adm with AVX2 differs from the scalar path";
    } else if (!same_bits(&scalar, &native)) {
        msg = "compute_adm with every instruction set differs from the scalar path";
    }
    aligned_free(ref);
    aligned_free(dis);
    return msg;
}

/* end to end: the dispatch in adm.c, on sizes whose four scales cross the
 * vector widths in different places. */
static char *test_compute_adm_is_the_same_on_every_dispatch_level(void)
{
    static const int SIZES[6][2] = {{576, 324}, {321, 243}, {130, 67},
                                    {65, 47},   {33, 18},   {17, 17}};
    uint32_t seed = 0xC0FFEE11u;
    for (int k = 0; k < 6; k++) {
        char *msg = compare_dispatch(SIZES[k][0], SIZES[k][1], &seed);
        if (msg) {
            return msg;
        }
    }
    return NULL;
}

static char *run_avx2_tests(void)
{
    mu_run_test(test_dwt2_avx2_matches_scalar_on_picture_data);
    mu_run_test(test_dwt2_avx2_matches_scalar_on_signed_zeros);
    mu_run_test(test_dwt2_avx2_matches_scalar_on_special_values);
    mu_run_test(test_csf_avx2_matches_scalar);
    return NULL;
}

static char *run_avx512_tests(void)
{
#if HAVE_AVX512
    mu_run_test(test_dwt2_avx512_matches_scalar_on_picture_data);
    mu_run_test(test_dwt2_avx512_matches_scalar_on_signed_zeros);
    mu_run_test(test_dwt2_avx512_matches_scalar_on_special_values);
    mu_run_test(test_csf_avx512_matches_scalar);
#endif
    return NULL;
}

char *run_tests(void)
{
    vmaf_init_cpu();
    const unsigned flags = vmaf_get_cpu_flags();
    char *msg = NULL;

    if (flags & (unsigned)VMAF_X86_CPU_FLAG_AVX2) {
        msg = run_avx2_tests();
    }
    if (!msg && (flags & (unsigned)VMAF_X86_CPU_FLAG_AVX512)) {
        msg = run_avx512_tests();
    }
    if (msg) {
        return msg;
    }
    mu_run_test(test_compute_adm_is_the_same_on_every_dispatch_level);
    return NULL;
}

#else /* !ARCH_X86 */

char *run_tests(void)
{
    return NULL;
}

#endif /* ARCH_X86 */

/* NOLINTEND(modernize-use-nullptr) */
