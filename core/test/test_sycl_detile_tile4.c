/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * Tile4 luma de-tile kernel of the SYCL zero-copy import (ADR-1769, K3).
 *
 * Builds Tile4 luma planes on the host with a forward address function written
 * from the tiling definition (a bit-source string, not the kernel's shifts and
 * masks), fills every byte of the tiled object (row padding and the tiles past
 * the frame too) with random data, runs vmaf_sycl_detile_tile4_for_test() and
 * compares the linear output with the expected plane byte for byte, including
 * the P010 / P012 `>> (16 - bpc)` shift. The geometries cover rows that are a
 * multiple of 16 bytes and rows that are not (whole 4-byte words plus a byte
 * tail), and a pitch wider than the row. The output buffer is poisoned first,
 * so a byte the kernel never writes cannot match.
 *
 * Without a SYCL device the kernel cases print "[skip: no SYCL device]"; on a
 * build without the DMA-BUF import they print "[skip: no DMA-BUF import]".
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test.h"

#include "libvmaf/libvmaf_sycl.h"
#include "sycl/common.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy also proposes the `nullptr` keyword, but this is a C
 * translation unit whose sources spell the null pointer constant `NULL` and
 * MSVC's documented /std:clatest C23 feature set does not include `nullptr`
 * while the required Windows build compiles this TU with cl.exe. ADR-1138. */

typedef struct Geometry {
    unsigned w;     /* luma width in samples */
    unsigned h;     /* luma height */
    unsigned bpc;   /* 8: NV12, 10 / 12: P010 / P012 */
    unsigned widen; /* extra 128-byte tile columns beyond the row */
} Geometry;

static const Geometry g_geometries[] = {
    {3840u, 1600u, 10u, 0u}, /* 7680-byte rows, a multiple of 16 */
    {1920u, 1080u, 10u, 0u}, /* 3840-byte rows, a multiple of 128 */
    {1918u, 1080u, 10u, 0u}, /* 3836-byte rows: 12 bytes past the last full chunk */
    {640u, 360u, 8u, 0u},    /* NV12, 640-byte rows */
    {67u, 37u, 8u, 1u},      /* odd row: 3 bytes past a word, wider pitch */
    {250u, 70u, 12u, 1u},    /* P012, 500-byte rows: 4 bytes past a chunk */
};
#define N_GEOMETRIES (sizeof(g_geometries) / sizeof(g_geometries[0]))

static uint32_t g_rng = 0x9E3779B9u;

static uint32_t next_random(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

/* Byte offset inside a 4 KiB Tile4 tile, from per-bit sources (LSB first):
 * 'x' takes the next bit of the byte column (0..127), 'y' the next bit of the
 * row (0..31), as the Intel Tile4 definition interleaves them. */
static unsigned tile4_intra(unsigned x, unsigned y)
{
    const char *const spec = "xxxxyyxxyxyy";
    unsigned xi = 0u;
    unsigned yi = 0u;
    unsigned out = 0u;
    for (unsigned bit = 0u; bit < 12u; bit++) {
        const unsigned src = spec[bit] == 'x' ? (x >> xi++) : (y >> yi++);
        out |= (src & 1u) << bit;
    }
    return out;
}

static size_t tile4_addr(unsigned xb, unsigned y, size_t pitch)
{
    const size_t tiles_per_row = pitch / 128u;
    const size_t tile = ((size_t)(y / 32u) * tiles_per_row + xb / 128u) * 4096u;
    return tile + tile4_intra(xb % 128u, y % 32u);
}

typedef struct Plane {
    size_t row_bytes;
    size_t pitch;
    size_t tiled_size;
    unsigned h;
    unsigned bpc;
    uint8_t *tiled; /* host copy of the Tile4 object */
    uint8_t *want;  /* expected linear plane */
    uint8_t *got;
} Plane;

static int plane_alloc(Plane *p, const Geometry *g)
{
    const unsigned bps = g->bpc > 8u ? 2u : 1u;
    p->row_bytes = (size_t)g->w * bps;
    p->pitch = ((p->row_bytes + 127u) / 128u + g->widen) * 128u;
    p->h = g->h;
    p->bpc = g->bpc;
    p->tiled_size = (size_t)((g->h + 31u) / 32u) * (p->pitch / 128u) * 4096u;
    p->tiled = malloc(p->tiled_size);
    p->want = malloc(p->row_bytes * p->h);
    p->got = malloc(p->row_bytes * p->h);
    return p->tiled && p->want && p->got ? 0 : -ENOMEM;
}

static void plane_free(Plane *p)
{
    free(p->tiled);
    free(p->want);
    free(p->got);
}

/* Random bytes everywhere in the object; the expected plane reads them back at
 * their Tile4 addresses and shifts each little-endian 16-bit sample. */
static void plane_fill(Plane *p)
{
    for (size_t i = 0; i < p->tiled_size; i++)
        p->tiled[i] = (uint8_t)(next_random() >> 24);
    const unsigned shift = p->bpc > 8u ? 16u - p->bpc : 0u;
    for (unsigned y = 0u; y < p->h; y++) {
        uint8_t *const row = p->want + (size_t)y * p->row_bytes;
        for (size_t xb = 0; xb < p->row_bytes; xb++)
            row[xb] = p->tiled[tile4_addr((unsigned)xb, y, p->pitch)];
        for (size_t xb = 0; shift && xb + 1u < p->row_bytes; xb += 2u) {
            const unsigned s = ((unsigned)row[xb] | ((unsigned)row[xb + 1u] << 8)) >> shift;
            row[xb] = (uint8_t)(s & 0xFFu);
            row[xb + 1u] = (uint8_t)(s >> 8);
        }
    }
}

typedef struct DevicePlane {
    void *src;
    void *dst;
} DevicePlane;

static int device_upload(VmafSyclState *state, const Plane *p, DevicePlane *d)
{
    const size_t out_bytes = p->row_bytes * p->h;
    d->src = vmaf_sycl_malloc_device(state, p->tiled_size);
    d->dst = vmaf_sycl_malloc_device(state, out_bytes);
    if (!d->src || !d->dst)
        return -ENOMEM;
    memset(p->got, 0xCC, out_bytes);
    const int err = vmaf_sycl_memcpy_h2d(state, d->dst, p->got, out_bytes);
    return err | vmaf_sycl_memcpy_h2d(state, d->src, p->tiled, p->tiled_size);
}

static const char *detile_and_compare(VmafSyclState *state, Plane *p, const DevicePlane *d)
{
    const int err = vmaf_sycl_detile_tile4_for_test(state, d->dst, d->src, p->pitch, p->row_bytes,
                                                    p->h, p->bpc);
    mu_assert("de-tile run failed", !err);
    mu_assert("readback", !vmaf_sycl_memcpy_d2h(state, p->got, d->dst, p->row_bytes * p->h));
    mu_assert("de-tiled plane differs from the host Tile4 reference",
              !memcmp(p->got, p->want, p->row_bytes * p->h));
    return NULL;
}

static const char *run_case(VmafSyclState *state, const Geometry *g)
{
    Plane p;
    DevicePlane d = {NULL, NULL};
    memset(&p, 0, sizeof(p));
    const char *msg = NULL;
    if (plane_alloc(&p, g)) {
        msg = "host allocation failed";
    } else {
        plane_fill(&p);
        msg = device_upload(state, &p, &d) ? "device allocation or upload failed" :
                                             detile_and_compare(state, &p, &d);
    }
    if (msg)
        (void)fprintf(stderr, "\n%ux%u bpc=%u widen=%u: ", g->w, g->h, g->bpc, g->widen);
    vmaf_sycl_free(state, d.src);
    vmaf_sycl_free(state, d.dst);
    plane_free(&p);
    return msg;
}

static VmafSyclState *open_state(void)
{
    VmafSyclState *state = NULL;
    VmafSyclConfiguration cfg = {.device_index = -1};
    if (vmaf_sycl_state_init(&state, cfg) != 0 || !state) {
        (void)fprintf(stderr, "[skip: no SYCL device] ");
        mu_skipped = 1;
        return NULL;
    }
    return state;
}

/* A build without the DMA-BUF import has no kernel to test. The probe has no
 * rows, so a build with the import refuses it before any launch. */
static int import_built(VmafSyclState *state)
{
    static uint8_t probe[1];
    if (vmaf_sycl_detile_tile4_for_test(state, probe, probe, 128u, 1u, 0u, 8u) == -ENOSYS) {
        (void)fprintf(stderr, "[skip: no DMA-BUF import] ");
        mu_skipped = 1;
        return 0;
    }
    return 1;
}

static char *test_detile_matches_reference(void)
{
    VmafSyclState *state = open_state();
    if (!state)
        return NULL;
    const char *msg = NULL;
    if (import_built(state)) {
        for (size_t g = 0; g < N_GEOMETRIES && !msg; g++)
            msg = run_case(state, &g_geometries[g]);
    }
    vmaf_sycl_state_free(&state);
    return (char *)msg;
}

static char *test_rejects_bad_arguments(void)
{
    static uint8_t buf[256];
    mu_assert("NULL state",
              vmaf_sycl_detile_tile4_for_test(NULL, buf, buf, 128u, 64u, 1u, 8u) != 0);
    VmafSyclState *state = open_state();
    if (!state)
        return NULL;
    int ok = 1;
    if (import_built(state)) {
        ok &= vmaf_sycl_detile_tile4_for_test(state, NULL, buf, 128u, 64u, 1u, 8u) == -EINVAL;
        ok &= vmaf_sycl_detile_tile4_for_test(state, buf, buf, 100u, 64u, 1u, 8u) == -EINVAL;
        ok &= vmaf_sycl_detile_tile4_for_test(state, buf, buf, 128u, 129u, 1u, 8u) == -EINVAL;
        ok &= vmaf_sycl_detile_tile4_for_test(state, buf, buf, 128u, 64u, 0u, 8u) == -EINVAL;
        ok &= vmaf_sycl_detile_tile4_for_test(state, buf, buf, 128u, 64u, 1u, 17u) == -EINVAL;
    }
    vmaf_sycl_state_free(&state);
    mu_assert("a malformed argument must be refused with -EINVAL", ok);
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_rejects_bad_arguments);
    mu_run_test(test_detile_matches_reference);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
