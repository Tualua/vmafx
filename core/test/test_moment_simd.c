/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * Bit-exactness test for the float_moment SIMD kernels (T7-19, ADR-0179,
 * ADR-0584, ADR-0987, ADR-1500).
 *
 * The scalar reference (moment.c) adds every sample, or its float square,
 * into one double in raster order. Once that double passes 2^53 units of its
 * smallest term, every add rounds, so a kernel that groups the adds another
 * way (lane accumulators, per-row partial sums) returns another number. Every
 * SIMD kernel adds the values one after the other in raster order and must
 * return the scalar's bits: each comparison here is `==`.
 *
 * Two kinds of frame:
 *   - random floats in [0, 256) at small sizes, which exercise the vector
 *     loops and their tails;
 *   - 16-bit pictures as float_moment's picture_copy() hands them over
 *     (samples divided by 256), whose second-moment sum passes 2^53 units of
 *     2^-16: bright and dark noise at 3841x2160, and a frame whose sum is
 *     exactly 2^53 units before a last row of single units, each of which the
 *     scalar's double rounds away. The dark samples matter: a bright square is
 *     a multiple of 128 units, which no double below 2^60 units rounds. The frame that stops just below 2^53 is the
 *     control: every grouping agrees there.
 *
 * The SVE2 kernel is vector-length agnostic; run the binary under
 * `qemu-aarch64 -cpu max,sve128=on` (and sve256, sve512,
 * sve2048 with sve-default-vector-length=256) to cover each length.
 *
 * Boilerplate (xorshift PRNG, portable aligned alloc, AVX2 gate) is provided
 * by `simd_bitexact_test.h` (ADR-0245).
 */

#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "test.h"
/* clang-format off — `test.h` has no header guard, must precede the
 * harness include to avoid a `mu_report` redefinition. */
#include "simd_bitexact_test.h"
/* clang-format on */

#include "feature/moment.h"

#if ARCH_X86
#include "feature/x86/moment_avx2.h"
#if HAVE_AVX512
#include "feature/x86/moment_avx512.h"
#endif
#endif
#if ARCH_AARCH64
#include "feature/arm64/moment_neon.h"
#if HAVE_SVE2
#include "arm/cpu.h"
#include "feature/arm64/moment_sve2.h"
#endif
#endif

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy also proposes the `nullptr` keyword, but this is a C
 * translation unit whose sources spell the null pointer constant `NULL` and
 * MSVC's documented /std:clatest C23 feature set does not include `nullptr`
 * while the required Windows build compiles this TU with cl.exe. ADR-1138. */

#if ARCH_X86 || ARCH_AARCH64

#define ALIGN_BYTES 64
#define TEST_W 73 /* not a multiple of 4, 8 or 16: exercises every tail */
#define TEST_H 17

/* Pixel input range of the post-`picture_copy` float layout: [0, 256). */
#define MOMENT_FILL_LO 0.0f
#define MOMENT_FILL_HI 256.0f

/* A 16-bit sample as picture_copy() hands it over: divided by 256. */
#define MOMENT_16BIT_SCALE 256.0f

typedef int (*MomentFn)(const float *pic, int w, int h, int stride, double *score);

typedef struct MomentKernel {
    const char *name;
    MomentFn first;
    MomentFn second;
} MomentKernel;

/* One frame: `stride` in floats, `buf` aligned to ALIGN_BYTES. */
typedef struct MomentFrame {
    float *buf;
    int w;
    int h;
    int stride;
} MomentFrame;

/*
 * Tail-only regression fixture (moment-2nd-tail-double-square). A single row
 * narrower than a kernel's vector width runs only its scalar tail. The tail
 * once squared in double while the scalar squares in float. These values have
 * many significant bits, so the float square loses low mantissa bits.
 */
static const float moment_tail_vals[15] = {255.937f, 254.123f, 253.871f, 252.499f, 251.001f,
                                           250.753f, 249.317f, 248.111f, 247.629f, 246.003f,
                                           245.555f, 244.917f, 243.333f, 242.071f, 241.629f};

static int frame_alloc(MomentFrame *f, int w, int h)
{
    f->w = w;
    f->h = h;
    f->stride = (w + 15) & ~15;
    f->buf = (float *)simd_test_aligned_malloc((size_t)f->stride * (size_t)h * sizeof(float),
                                               ALIGN_BYTES);
    return f->buf ? 0 : -1;
}

/* Scalar against `k` on one frame, both moments, `==`. */
static char *check_frame(const MomentKernel *k, const MomentFrame *f, const char *what)
{
    const int stride_bytes = f->stride * (int)sizeof(float);
    double s_scalar = 0.0;
    double s_simd = 0.0;
    double t_scalar = 0.0;
    double t_simd = 0.0;
    int err = compute_1st_moment(f->buf, f->w, f->h, stride_bytes, &s_scalar);
    err |= k->first(f->buf, f->w, f->h, stride_bytes, &s_simd);
    err |= compute_2nd_moment(f->buf, f->w, f->h, stride_bytes, &t_scalar);
    err |= k->second(f->buf, f->w, f->h, stride_bytes, &t_simd);
    if (err || s_simd != s_scalar || t_simd != t_scalar) {
        (void)fprintf(stderr,
                      "\n%s %s %dx%d: 1st %.17g vs scalar %.17g, 2nd %.17g vs scalar %.17g\n",
                      k->name, what, f->w, f->h, s_simd, s_scalar, t_simd, t_scalar);
        return "a SIMD moment is not the scalar's bits";
    }
    return NULL;
}

static char *check_random(const MomentKernel *k, uint32_t seed, int w, int h)
{
    MomentFrame f;
    if (frame_alloc(&f, w, h)) {
        return "aligned_malloc failed";
    }
    simd_test_fill_random_f32(f.buf, (size_t)f.stride * (size_t)h, MOMENT_FILL_LO, MOMENT_FILL_HI,
                              seed);
    char *r = check_frame(k, &f, "random");
    simd_test_aligned_free(f.buf);
    return r;
}

static char *check_tail(const MomentKernel *k, int w)
{
    MomentFrame f;
    if (frame_alloc(&f, w, 1)) {
        return "aligned_malloc failed";
    }
    for (int j = 0; j < f.stride; ++j)
        f.buf[j] = moment_tail_vals[j % 15];
    char *r = check_frame(k, &f, "tail");
    simd_test_aligned_free(f.buf);
    return r;
}

/* One sample of 1 and fifteen of 1.5 * 2^-53, three quarters of the double's
 * last place at 1: each add of the scalar's running sum rounds up by one
 * place (1 + 15 places), where four lane sums give 1 + 12 places. The first
 * moment needs the scalar's order too on inputs that are not
 * picture_copy() values. */
static char *check_fine_and_coarse(const MomentKernel *k)
{
    MomentFrame f;
    if (frame_alloc(&f, 16, 1)) {
        return "aligned_malloc failed";
    }
    f.buf[0] = 1.0f;
    for (int j = 1; j < f.stride; ++j)
        f.buf[j] = 1.5f / 9007199254740992.0f; /* 2^53 */
    char *r = check_frame(k, &f, "fine and coarse floats");
    simd_test_aligned_free(f.buf);
    return r;
}

/* 16-bit noise, three samples in four bright (in [49152, 65535]) and one
 * dark (odd, below 4096). At 3841x2160 the second moment's sum is about
 * 2^54.2 units of 2^-16. A bright square is a float above 2^31 units, a
 * multiple of 128 units, which a double near 2^54 adds exactly; a dark square
 * is an odd number of units, which it rounds. The odd width runs every
 * kernel's tail past 2^53. */
static char *check_bright_dark_noise(const MomentKernel *k)
{
    MomentFrame f;
    if (frame_alloc(&f, 3841, 2160)) {
        return "aligned_malloc failed";
    }
    uint32_t state = 0x5eed1234u;
    for (size_t i = 0; i < (size_t)f.stride * (size_t)f.h; ++i) {
        const uint32_t r = simd_test_xorshift32(&state);
        const uint32_t s = (r & 3u) ? 49152u + ((r >> 2) & 0x3fffu) : ((r >> 2) & 0xfffu) | 1u;
        f.buf[i] = (float)s / MOMENT_16BIT_SCALE;
    }
    char *r = check_frame(k, &f, "bright and dark noise past 2^53");
    simd_test_aligned_free(f.buf);
    return r;
}

/* `bright` rows of sample 32768 (square 2^30 units of 2^-16, exact), then
 * rows of sample 1 (square 1 unit). With 4096 columns, 2048 bright rows add
 * to 2^53 units exactly; every unit after them is a tie the scalar's double
 * rounds to even, back to 2^53, where a grouped sum keeps it. */
static char *check_units_after(const MomentKernel *k, int h, int bright, const char *what)
{
    MomentFrame f;
    if (frame_alloc(&f, 4096, h)) {
        return "aligned_malloc failed";
    }
    for (int i = 0; i < h; ++i) {
        const float v = i < bright ? 32768.0f / MOMENT_16BIT_SCALE : 1.0f / MOMENT_16BIT_SCALE;
        for (int j = 0; j < f.stride; ++j)
            f.buf[((size_t)i * (size_t)f.stride) + (size_t)j] = v;
    }
    char *r = check_frame(k, &f, what);
    simd_test_aligned_free(f.buf);
    return r;
}

/* Every frame of this test against one kernel. Every frame runs and every
 * mismatch is printed; the first one's message is returned. */
static char *check_kernel(const MomentKernel *k)
{
    static const uint32_t seeds[3] = {0xdeadbeefu, 0x12345678u, 0xabcdef01u};
    static const int widths[5] = {TEST_W, 64, 5, 9, 15};
    static const int heights[5] = {TEST_H, 16, 1, 1, 1};
    char *first = NULL;
    char *r = NULL;
    for (int c = 0; c < 5; ++c) {
        r = check_random(k, seeds[c % 3], widths[c], heights[c]);
        first = first ? first : r;
    }
    for (int w = 1; w <= 15; ++w) {
        r = check_tail(k, w);
        first = first ? first : r;
    }
    r = check_fine_and_coarse(k);
    first = first ? first : r;
    r = check_units_after(k, 2048, 2047, "sum just below 2^53 (control)");
    first = first ? first : r;
    r = check_units_after(k, 2049, 2048, "sum 2^53, then 4096 single units");
    first = first ? first : r;
    r = check_bright_dark_noise(k);
    return first ? first : r;
}

#endif /* ARCH_X86 || ARCH_AARCH64 */

#if ARCH_X86

static char *test_avx2_scalar_bits(void)
{
    static const MomentKernel k = {"avx2", compute_1st_moment_avx2, compute_2nd_moment_avx2};
    return check_kernel(&k);
}

#if HAVE_AVX512
static char *test_avx512_scalar_bits(void)
{
    static const MomentKernel k = {"avx512", compute_1st_moment_avx512, compute_2nd_moment_avx512};
    return check_kernel(&k);
}
#endif /* HAVE_AVX512 */

#endif /* ARCH_X86 */

#if ARCH_AARCH64

static char *test_neon_scalar_bits(void)
{
    static const MomentKernel k = {"neon", compute_1st_moment_neon, compute_2nd_moment_neon};
    return check_kernel(&k);
}

#if HAVE_SVE2
/* SVE2 (ADR-0584). Runtime-skipped when the processor lacks SVE2, so a
 * NEON-only host passes without executing the SVE2 path. The probe is
 * vmaf_get_cpu_flags_arm(): vmaf_get_cpu_flags() reads 0 until
 * vmaf_init_cpu() has run, which this test never calls, and skipped it on every
 * processor until ADR-1500. */
static char *test_sve2_scalar_bits(void)
{
    if (!(vmaf_get_cpu_flags_arm() & VMAF_ARM_CPU_FLAG_SVE2)) {
        (void)fprintf(stderr, "  skipping SVE2 moment test: HWCAP2_SVE2 not set\n");
        return NULL;
    }
    static const MomentKernel k = {"sve2", compute_1st_moment_sve2, compute_2nd_moment_sve2};
    return check_kernel(&k);
}
#endif /* HAVE_SVE2 */

#endif /* ARCH_AARCH64 */

/* Each kernel runs and reports even when another one failed. */
static char *report_all(const char *name, char *(*test)(void), char *first)
{
    char *r = mu_report(name, test);
    return first ? first : r;
}

char *run_tests(void)
{
    char *first = NULL;
#if ARCH_X86
    if (simd_test_have_avx2()) {
        first = report_all("test_avx2_scalar_bits", test_avx2_scalar_bits, first);
#if HAVE_AVX512
        if (simd_test_have_avx512()) {
            first = report_all("test_avx512_scalar_bits", test_avx512_scalar_bits, first);
        }
#endif /* HAVE_AVX512 */
    }
#elif ARCH_AARCH64
    first = report_all("test_neon_scalar_bits", test_neon_scalar_bits, first);
#if HAVE_SVE2
    first = report_all("test_sve2_scalar_bits", test_sve2_scalar_bits, first);
#endif
#else
    (void)fprintf(stderr, "skipping: arch lacks moment SIMD\n");
#endif
    return first;
}

/* NOLINTEND(modernize-use-nullptr) */
