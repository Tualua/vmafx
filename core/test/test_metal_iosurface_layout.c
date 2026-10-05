/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * core/src/metal/iosurface_layout.h, the source layouts of the Metal
 * IOSurface import (ADR-1679), on every host. VideoToolbox decodes to NV12
 * and P010: one luma plane and one plane of interleaved Cb/Cr, P010 with its
 * 10 bits in the most significant bits of 16. The import must hand libvmaf
 * planar 4:2:0 with the samples in the least significant bits; before
 * ADR-1679 it copied the interleaved plane as if it were the Cb plane and
 * left P010 unshifted. The fixtures here are laid out as CoreVideo lays out
 * those surfaces (row padding filled with a sentinel), so the de-interleave,
 * the shift and every refusal are checked on the code the .mm runs.
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "test.h"

#include "metal/iosurface_layout.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit; MSVC's C mode has
 * no `nullptr`. ADR-1138. */

#define PAD 0xEEu
#define SRC_STRIDE ((size_t)32u)
#define DST_STRIDE ((size_t)24u)
#define ROWS 4u

typedef unsigned (*SampleFn)(unsigned plane, unsigned y, unsigned x);

static const VmafMetalSurfaceFormat *fmt_of(char a, char b, char c, char d)
{
    return vmaf_metal_surface_format(VMAF_METAL_FOURCC(a, b, c, d));
}

typedef struct FormatCase {
    uint32_t fourcc;
    unsigned planes, bpc, shift;
    const char *name;
} FormatCase;

static int format_is(const FormatCase *c)
{
    const VmafMetalSurfaceFormat *f = vmaf_metal_surface_format(c->fourcc);
    if (f == NULL) {
        return 0;
    }
    return f->planes == c->planes && f->bpc == c->bpc && f->shift == c->shift &&
           strcmp(f->name, c->name) == 0;
}

static char *test_format_table(void)
{
    static const FormatCase cases[] = {
        {VMAF_METAL_FOURCC('4', '2', '0', 'v'), 2u, 8u, 0u, "nv12"},
        {VMAF_METAL_FOURCC('4', '2', '0', 'f'), 2u, 8u, 0u, "nv12"},
        {VMAF_METAL_FOURCC('x', '4', '2', '0'), 2u, 10u, 6u, "p010"},
        {VMAF_METAL_FOURCC('x', 'f', '2', '0'), 2u, 10u, 6u, "p010"},
        {VMAF_METAL_FOURCC('y', '4', '2', '0'), 3u, 8u, 0u, "yuv420p"},
        {VMAF_METAL_FOURCC('f', '4', '2', '0'), 3u, 8u, 0u, "yuv420p"},
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        mu_assert("a VideoToolbox 4:2:0 layout has its planes, depth and shift",
                  format_is(&cases[i]));
    }
    return NULL;
}

static char *test_unsupported_formats_refused(void)
{
    /* BGRA, 4:2:2 P210 ('x422'), 4:4:4 NV24 ('444v'), v210, and 0. */
    static const uint32_t refused[] = {
        VMAF_METAL_FOURCC('B', 'G', 'R', 'A'), VMAF_METAL_FOURCC('x', '4', '2', '2'),
        VMAF_METAL_FOURCC('4', '4', '4', 'v'), VMAF_METAL_FOURCC('v', '2', '1', '0'), 0u};
    for (size_t i = 0u; i < sizeof(refused) / sizeof(refused[0]); i++) {
        mu_assert("a layout outside the table is refused",
                  vmaf_metal_surface_format(refused[i]) == NULL);
    }
    return NULL;
}

/* Plan and read plane `plane` of a w x h picture from the source planes. */
static int read_plane(const VmafMetalSurfaceFormat *fmt, const uint8_t *const *src,
                      const VmafMetalSurfacePlane *geo, unsigned plane, unsigned w, unsigned h,
                      uint8_t *dst)
{
    const unsigned pw = plane ? (w + 1u) / 2u : w;
    const unsigned ph = plane ? (h + 1u) / 2u : h;
    VmafMetalPlaneRead rd;
    const unsigned sp = vmaf_metal_surface_src_plane(fmt, plane);
    const int err =
        vmaf_metal_plane_read_plan(fmt, fmt->planes, plane, fmt->bpc, &geo[sp], pw, ph, &rd);
    if (err) {
        return err;
    }
    vmaf_metal_read_plane(dst, DST_STRIDE, src[sp], SRC_STRIDE, pw, ph, &rd);
    return 0;
}

static unsigned get_sample(const uint8_t *row, unsigned bytes, unsigned idx)
{
    if (bytes == 1u) {
        return row[idx];
    }
    uint16_t v = 0u;
    memcpy(&v, row + (size_t)idx * 2u, sizeof(v));
    return v;
}

static void put_sample(uint8_t *row, unsigned bytes, unsigned idx, unsigned v)
{
    if (bytes == 1u) {
        row[idx] = (uint8_t)v;
        return;
    }
    const uint16_t s = (uint16_t)v;
    memcpy(row + (size_t)idx * 2u, &s, sizeof(s));
}

/* True when the w x h plane `plane` of `out` holds f(plane, y, x). */
static int plane_matches(const uint8_t *out, unsigned bytes, unsigned plane, unsigned w, unsigned h,
                         SampleFn f)
{
    for (unsigned y = 0u; y < h; y++) {
        for (unsigned x = 0u; x < w; x++) {
            if (get_sample(out + (size_t)y * DST_STRIDE, bytes, x) != f(plane, y, x)) {
                return 0;
            }
        }
    }
    return 1;
}

/* A bi-planar surface as CoreVideo lays it out: Y rows, then Cb/Cr pairs,
 * each sample stored as f(...) << shift, row padding set to PAD. */
static void fill_biplanar(uint8_t *y_plane, uint8_t *uv_plane, unsigned w, unsigned h,
                          unsigned bytes, unsigned shift, SampleFn f)
{
    memset(y_plane, (int)PAD, ROWS * SRC_STRIDE);
    memset(uv_plane, (int)PAD, ROWS * SRC_STRIDE);
    for (unsigned y = 0u; y < h; y++) {
        for (unsigned x = 0u; x < w; x++) {
            put_sample(y_plane + (size_t)y * SRC_STRIDE, bytes, x, f(0u, y, x) << shift);
        }
    }
    for (unsigned y = 0u; y < (h + 1u) / 2u; y++) {
        uint8_t *row = uv_plane + (size_t)y * SRC_STRIDE;
        for (unsigned x = 0u; x < (w + 1u) / 2u; x++) {
            put_sample(row, bytes, 2u * x, f(1u, y, x) << shift);
            put_sample(row, bytes, 2u * x + 1u, f(2u, y, x) << shift);
        }
    }
}

/* NV12, 6x4 (chroma 3x2): Y = 16y + x, Cb = 100 + 10y + x, Cr = 200 + 10y + x. */
static unsigned nv12_value(unsigned plane, unsigned y, unsigned x)
{
    return (plane == 0u) ? 16u * y + x : 100u * plane + 10u * y + x;
}

/* P010, 5x3 (odd: chroma 3x2), with the largest 10-bit sample (0xFFC0). */
static unsigned p010_value(unsigned plane, unsigned y, unsigned x)
{
    if (plane == 0u && y == 2u && x == 4u) {
        return 1023u;
    }
    return plane * 300u + 64u * y + 7u * x + 1u;
}

/* Read all three planes of a bi-planar w x h frame and compare them with f. */
static char *biplanar_reads_back(const VmafMetalSurfaceFormat *fmt, unsigned w, unsigned h,
                                 SampleFn f)
{
    const unsigned bytes = (fmt->bpc > 8u) ? 2u : 1u;
    uint8_t y_plane[ROWS * SRC_STRIDE];
    uint8_t uv_plane[ROWS * SRC_STRIDE];
    fill_biplanar(y_plane, uv_plane, w, h, bytes, fmt->shift, f);
    const uint8_t *src[2] = {y_plane, uv_plane};
    const VmafMetalSurfacePlane geo[2] = {
        {w, h, bytes, SRC_STRIDE}, {(w + 1u) / 2u, (h + 1u) / 2u, (size_t)2u * bytes, SRC_STRIDE}};
    uint8_t out[ROWS * DST_STRIDE];
    for (unsigned p = 0u; p < 3u; p++) {
        memset(out, 0, sizeof(out));
        mu_assert("bi-planar plane planned and read", read_plane(fmt, src, geo, p, w, h, out) == 0);
        const unsigned pw = p ? (w + 1u) / 2u : w;
        const unsigned ph = p ? (h + 1u) / 2u : h;
        mu_assert("plane de-interleaved and shifted", plane_matches(out, bytes, p, pw, ph, f));
        mu_assert("no write past the plane's row", get_sample(out, bytes, pw) == 0u);
    }
    return NULL;
}

static char *test_nv12_deinterleaved(void)
{
    return biplanar_reads_back(fmt_of('4', '2', '0', 'v'), 6u, 4u, nv12_value);
}

static char *test_p010_deinterleaved_and_shifted(void)
{
    return biplanar_reads_back(fmt_of('x', '4', '2', '0'), 5u, 3u, p010_value);
}

/* The pre-ADR-1679 import copied the interleaved plane as the Cb plane. */
static char *test_fixture_tells_interleaved_from_planar(void)
{
    uint8_t y_plane[ROWS * SRC_STRIDE];
    uint8_t uv_plane[ROWS * SRC_STRIDE];
    fill_biplanar(y_plane, uv_plane, 6u, 4u, 1u, 0u, nv12_value);
    mu_assert("Cb row 0 differs from the interleaved bytes",
              !plane_matches(uv_plane, 1u, 1u, 3u, 1u, nv12_value));
    return NULL;
}

static char *test_planar_reads_its_own_plane(void)
{
    uint8_t planes[3][2u * SRC_STRIDE];
    for (unsigned p = 0u; p < 3u; p++) {
        memset(planes[p], (int)(10u * (p + 1u)), sizeof(planes[p]));
    }
    const uint8_t *src[3] = {planes[0], planes[1], planes[2]};
    const VmafMetalSurfacePlane geo[3] = {
        {4u, 2u, 1u, SRC_STRIDE}, {2u, 1u, 1u, SRC_STRIDE}, {2u, 1u, 1u, SRC_STRIDE}};
    uint8_t out[2u * DST_STRIDE];
    mu_assert("planar Cr planned and read",
              read_plane(fmt_of('y', '4', '2', '0'), src, geo, 2u, 4u, 2u, out) == 0);
    mu_assert("planar Cr comes from surface plane 2", out[0] == 30u && out[1] == 30u);
    return NULL;
}

typedef struct PlanCase {
    const char *what;
    size_t plane_count;
    VmafMetalSurfacePlane src;
    uint32_t fourcc;
    unsigned plane, bpc, dst_w, dst_h;
    int expect;
} PlanCase;

#define NV12_CC VMAF_METAL_FOURCC('4', '2', '0', 'v')
#define P010_CC VMAF_METAL_FOURCC('x', '4', '2', '0')

static char *test_plan_refusals(void)
{
    static const PlanCase cases[] = {
        {"NV12 Cb", 2u, {3u, 2u, 2u, 32u}, NV12_CC, 1u, 8u, 3u, 2u, 0},
        {"plane 3", 2u, {3u, 2u, 2u, 32u}, NV12_CC, 3u, 8u, 3u, 2u, -EINVAL},
        {"3 planes are not NV12", 3u, {3u, 2u, 2u, 32u}, NV12_CC, 1u, 8u, 3u, 2u, -EINVAL},
        {"P010 at bpc 8", 2u, {3u, 2u, 2u, 32u}, P010_CC, 1u, 8u, 3u, 2u, -EINVAL},
        {"NV12 at bpc 10", 2u, {3u, 2u, 2u, 32u}, NV12_CC, 1u, 10u, 3u, 2u, -EINVAL},
        {"1-byte CbCr element", 2u, {3u, 2u, 1u, 32u}, NV12_CC, 1u, 8u, 3u, 2u, -EINVAL},
        {"source narrower", 2u, {3u, 2u, 2u, 32u}, NV12_CC, 1u, 8u, 4u, 2u, -EINVAL},
        {"source shorter", 2u, {3u, 2u, 2u, 32u}, NV12_CC, 2u, 8u, 3u, 3u, -EINVAL},
        {"row bytes below width x element", 2u, {3u, 2u, 2u, 5u}, NV12_CC, 1u, 8u, 3u, 2u, -EINVAL},
        {"row bytes of width x element", 2u, {3u, 2u, 2u, 6u}, NV12_CC, 1u, 8u, 3u, 2u, 0},
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const PlanCase *c = &cases[i];
        VmafMetalPlaneRead rd;
        const int got =
            vmaf_metal_plane_read_plan(vmaf_metal_surface_format(c->fourcc), c->plane_count,
                                       c->plane, c->bpc, &c->src, c->dst_w, c->dst_h, &rd);
        if (got != c->expect) {
            (void)fprintf(stderr, "plan case \"%s\": %d, expected %d\n", c->what, got, c->expect);
        }
        mu_assert("a plan accepts or refuses as the table says", got == c->expect);
    }
    VmafMetalPlaneRead rd;
    const VmafMetalSurfacePlane uv = {3u, 2u, 2u, 32u};
    mu_assert("no format",
              vmaf_metal_plane_read_plan(NULL, 2u, 1u, 8u, &uv, 3u, 2u, &rd) == -EINVAL);
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_format_table);
    mu_run_test(test_unsupported_formats_refused);
    mu_run_test(test_nv12_deinterleaved);
    mu_run_test(test_p010_deinterleaved_and_shifted);
    mu_run_test(test_fixture_tells_interleaved_from_planar);
    mu_run_test(test_planar_reads_its_own_plane);
    mu_run_test(test_plan_refusals);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
