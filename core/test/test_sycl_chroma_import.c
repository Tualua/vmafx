/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * Chroma de-interleave kernel of the SYCL zero-copy import (ADR-1597).
 *
 * A decoder's UV plane is interleaved (NV12: U8 V8, P010: U16 V16, samples
 * MSB-aligned) and, on Intel GPUs, tiled. This gate builds such planes on the
 * host with a forward address function written from the tiling definitions
 * (bit-source strings, not the kernel's shifts and masks), uploads them as
 * device USM, runs vmaf_sycl_chroma_import_launch() and compares the planar Cb
 * and Cr it writes byte for byte. It covers LINEAR, Tile4 and Y-tiled, 8, 10,
 * 12 and 16 bit, 576x324, 1920x1080, odd 67x37 (partial tiles) and a pitch
 * wider than the row. P010 vectors carry non-zero low bits so a missing or
 * doubled shift fails.
 *
 * The descriptor validator and the modifier mapping need no device and always
 * run. Without a SYCL device the kernel cases print "[skip: no SYCL device]".
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test.h"

#include "libvmaf/libvmaf_sycl.h"
#include "sycl/chroma_import.h"
#include "sycl/common.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy also proposes the `nullptr` keyword, but this is a C
 * translation unit whose sources spell the null pointer constant `NULL` and
 * MSVC's documented /std:clatest C23 feature set does not include `nullptr`
 * while the required Windows build compiles this TU with cl.exe. ADR-1138. */

#define MOD_LINEAR 0ULL
#define MOD_TILE4 0x0100000000000009ULL
#define MOD_YTILED 0x0100000000000002ULL
#define MOD_XTILED 0x0100000000000001ULL

typedef struct Geometry {
    unsigned w;     /* luma width */
    unsigned h;     /* luma height */
    unsigned widen; /* 1: pitch is wider than the row of pairs */
    size_t offset;  /* plane offset in the object; tile-aligned for tiled layouts */
} Geometry;

static const Geometry g_geometries[] = {
    {576u, 324u, 0u, 0u},
    {1920u, 1080u, 0u, 4096u * 3u},
    {67u, 37u, 0u, 0u},
    {250u, 138u, 1u, 4096u},
};
#define N_GEOMETRIES (sizeof(g_geometries) / sizeof(g_geometries[0]))

static const unsigned g_bpcs[] = {8u, 10u, 12u, 16u};
#define N_BPCS (sizeof(g_bpcs) / sizeof(g_bpcs[0]))

static const enum VmafSyclChromaLayout g_layouts[] = {
    VMAF_SYCL_CHROMA_LINEAR,
    VMAF_SYCL_CHROMA_TILE4,
    VMAF_SYCL_CHROMA_YTILED,
};
#define N_LAYOUTS (sizeof(g_layouts) / sizeof(g_layouts[0]))

static uint32_t g_rng = 0x2545F491u;

static uint32_t next_random(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

/* Byte offset inside a 4 KiB tile, from per-bit sources (LSB first): 'x' takes
 * the next bit of the byte column (0..127), 'y' the next bit of the row (0..31).
 * Tile4 interleaves them (Intel Tile4 definition); Y-tiled is column-major
 * 16-byte OWords: x[3:0], then y[4:0], then x[6:4]. */
static unsigned intra_tile(enum VmafSyclChromaLayout layout, unsigned x, unsigned y)
{
    const char *const spec = layout == VMAF_SYCL_CHROMA_TILE4 ? "xxxxyyxxyxyy" : "xxxxyyyyyxxx";
    unsigned xi = 0u;
    unsigned yi = 0u;
    unsigned out = 0u;
    for (unsigned bit = 0u; bit < 12u; bit++) {
        const unsigned src = spec[bit] == 'x' ? (x >> xi++) : (y >> yi++);
        out |= (src & 1u) << bit;
    }
    return out;
}

/* Byte address of (byte column xb, row y) relative to the plane start. */
static size_t forward_addr(enum VmafSyclChromaLayout layout, unsigned xb, unsigned y,
                           unsigned pitch)
{
    if (layout == VMAF_SYCL_CHROMA_LINEAR)
        return (size_t)y * pitch + xb;
    const size_t tiles_per_row = pitch / 128u;
    const size_t tile = ((size_t)(y / 32u) * tiles_per_row + xb / 128u) * 4096u;
    return tile + intra_tile(layout, xb % 128u, y % 32u);
}

typedef struct Plane {
    enum VmafSyclChromaLayout layout;
    unsigned bpc;
    unsigned bps;
    unsigned cw;
    unsigned ch;
    unsigned pitch;
    size_t offset;
    size_t object_size;
    uint8_t *surface; /* host copy of the object */
    uint8_t *want_cb; /* expected planar Cb, tight pitch */
    uint8_t *want_cr;
} Plane;

static size_t plane_extent(const Plane *p)
{
    if (p->layout == VMAF_SYCL_CHROMA_LINEAR)
        return (size_t)(p->ch - 1u) * p->pitch + (size_t)p->cw * 2u * p->bps;
    return (size_t)((p->ch + 31u) / 32u) * (p->pitch / 128u) * 4096u;
}

static void plane_geometry(Plane *p, const Geometry *g)
{
    const unsigned row = ((g->w + 1u) / 2u) * 2u * p->bps;
    p->cw = (g->w + 1u) / 2u;
    p->ch = (g->h + 1u) / 2u;
    p->offset = g->offset;
    if (p->layout == VMAF_SYCL_CHROMA_LINEAR)
        p->pitch = row + (g->widen ? 64u : 0u);
    else
        p->pitch = ((row + 127u) / 128u) * 128u + (g->widen ? 128u : 0u);
    p->object_size = p->offset + plane_extent(p);
}

static void put_sample(uint8_t *dst, unsigned bps, unsigned value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    if (bps == 2u)
        dst[1] = (uint8_t)(value >> 8);
}

/* Fill the host surface with random UV pairs at their forward addresses and
 * the expected planar planes, shifted right by 16 - bpc for bpc > 8. */
static void plane_fill(Plane *p)
{
    const unsigned shift = p->bpc > 8u ? 16u - p->bpc : 0u;
    const unsigned mask = p->bps == 2u ? 0xFFFFu : 0xFFu;
    memset(p->surface, 0xA5, p->object_size);
    for (unsigned y = 0u; y < p->ch; y++) {
        for (unsigned x = 0u; x < p->cw; x++) {
            const unsigned u = next_random() & mask;
            const unsigned v = next_random() & mask;
            uint8_t *const at =
                p->surface + p->offset + forward_addr(p->layout, x * 2u * p->bps, y, p->pitch);
            put_sample(at, p->bps, u);
            put_sample(at + p->bps, p->bps, v);
            const size_t idx = ((size_t)y * p->cw + x) * p->bps;
            put_sample(p->want_cb + idx, p->bps, u >> shift);
            put_sample(p->want_cr + idx, p->bps, v >> shift);
        }
    }
}

static int plane_alloc(Plane *p, enum VmafSyclChromaLayout layout, unsigned bpc, const Geometry *g)
{
    p->layout = layout;
    p->bpc = bpc;
    p->bps = (bpc + 7u) / 8u;
    plane_geometry(p, g);
    const size_t out_bytes = (size_t)p->cw * p->ch * p->bps;
    p->surface = malloc(p->object_size);
    p->want_cb = malloc(out_bytes);
    p->want_cr = malloc(out_bytes);
    return p->surface && p->want_cb && p->want_cr ? 0 : -ENOMEM;
}

static void plane_free(Plane *p)
{
    free(p->surface);
    free(p->want_cb);
    free(p->want_cr);
}

typedef struct DevicePlane {
    void *src;
    void *cb;
    void *cr;
    uint8_t *got_cb;
    uint8_t *got_cr;
} DevicePlane;

static void device_free(VmafSyclState *state, DevicePlane *d)
{
    vmaf_sycl_free(state, d->src);
    vmaf_sycl_free(state, d->cb);
    vmaf_sycl_free(state, d->cr);
    free(d->got_cb);
    free(d->got_cr);
}

static int device_alloc(VmafSyclState *state, const Plane *p, DevicePlane *d)
{
    const size_t out_bytes = (size_t)p->cw * p->ch * p->bps;
    d->src = vmaf_sycl_malloc_device(state, p->object_size);
    d->cb = vmaf_sycl_malloc_device(state, out_bytes);
    d->cr = vmaf_sycl_malloc_device(state, out_bytes);
    d->got_cb = malloc(out_bytes);
    d->got_cr = malloc(out_bytes);
    if (!d->src || !d->cb || !d->cr || !d->got_cb || !d->got_cr)
        return -ENOMEM;
    /* Poison the outputs so a sample the kernel never writes cannot match. */
    memset(d->got_cb, 0xCC, out_bytes);
    int err = vmaf_sycl_memcpy_h2d(state, d->cb, d->got_cb, out_bytes);
    err |= vmaf_sycl_memcpy_h2d(state, d->cr, d->got_cb, out_bytes);
    return err | vmaf_sycl_memcpy_h2d(state, d->src, p->surface, p->object_size);
}

/* Launch, read both planes back and compare. `async` exercises the event path. */
static const char *convert_and_compare(VmafSyclState *state, const Plane *p, DevicePlane *d,
                                       int async)
{
    const VmafSyclChromaSrc src = {.base = d->src,
                                   .layout = p->layout,
                                   .offset = p->offset,
                                   .pitch = p->pitch,
                                   .cw = p->cw,
                                   .ch = p->ch,
                                   .bpc = p->bpc};
    void *event = NULL;
    const int err = vmaf_sycl_chroma_import_launch(state, &src, p->object_size, d->cb, d->cr,
                                                   async ? &event : NULL);
    mu_assert("chroma import launch failed", !err);
    const size_t out_bytes = (size_t)p->cw * p->ch * p->bps;
    mu_assert("Cb readback", !vmaf_sycl_memcpy_d2h(state, d->got_cb, d->cb, out_bytes));
    mu_assert("Cr readback", !vmaf_sycl_memcpy_d2h(state, d->got_cr, d->cr, out_bytes));
    vmaf_sycl_chroma_event_free(event);
    mu_assert("Cb plane differs from the planar reference",
              !memcmp(d->got_cb, p->want_cb, out_bytes));
    mu_assert("Cr plane differs from the planar reference",
              !memcmp(d->got_cr, p->want_cr, out_bytes));
    return NULL;
}

static const char *run_case(VmafSyclState *state, enum VmafSyclChromaLayout layout, unsigned bpc,
                            const Geometry *g)
{
    Plane p;
    DevicePlane d;
    memset(&p, 0, sizeof(p));
    memset(&d, 0, sizeof(d));
    const char *msg = NULL;
    if (plane_alloc(&p, layout, bpc, g)) {
        msg = "host allocation failed";
    } else {
        plane_fill(&p);
        if (device_alloc(state, &p, &d))
            msg = "device allocation or upload failed";
        else
            msg = convert_and_compare(state, &p, &d, g->w == 67u);
    }
    if (msg) {
        (void)fprintf(stderr, "\nlayout=%d bpc=%u %ux%u widen=%u: ", (int)layout, bpc, g->w, g->h,
                      g->widen);
    }
    device_free(state, &d);
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

static char *test_kernel_matches_reference(void)
{
    VmafSyclState *state = open_state();
    if (!state)
        return NULL;
    const char *msg = NULL;
    for (size_t l = 0; l < N_LAYOUTS && !msg; l++) {
        for (size_t b = 0; b < N_BPCS && !msg; b++) {
            for (size_t g = 0; g < N_GEOMETRIES && !msg; g++)
                msg = run_case(state, g_layouts[l], g_bpcs[b], &g_geometries[g]);
        }
    }
    vmaf_sycl_state_free(&state);
    return (char *)msg;
}

static char *test_modifier_mapping(void)
{
    enum VmafSyclChromaLayout layout = VMAF_SYCL_CHROMA_YTILED;
    mu_assert("linear", !vmaf_sycl_chroma_layout_from_modifier(MOD_LINEAR, &layout) &&
                            layout == VMAF_SYCL_CHROMA_LINEAR);
    mu_assert("tile4", !vmaf_sycl_chroma_layout_from_modifier(MOD_TILE4, &layout) &&
                           layout == VMAF_SYCL_CHROMA_TILE4);
    mu_assert("y-tiled", !vmaf_sycl_chroma_layout_from_modifier(MOD_YTILED, &layout) &&
                             layout == VMAF_SYCL_CHROMA_YTILED);
    mu_assert("x-tiled is not supported",
              vmaf_sycl_chroma_layout_from_modifier(MOD_XTILED, &layout) == -ENOTSUP);
    mu_assert("unknown modifier is not supported",
              vmaf_sycl_chroma_layout_from_modifier(0xDEADBEEFULL, &layout) == -ENOTSUP);
    mu_assert("NULL out", vmaf_sycl_chroma_layout_from_modifier(MOD_TILE4, NULL) == -EINVAL);
    return NULL;
}

static VmafSyclChromaSrc valid_src(enum VmafSyclChromaLayout layout)
{
    static const char dummy[1] = {0};
    VmafSyclChromaSrc src = {.base = dummy,
                             .layout = layout,
                             .offset = layout == VMAF_SYCL_CHROMA_LINEAR ? 0u : 225280u,
                             .pitch = 640u,
                             .cw = 288u,
                             .ch = 162u,
                             .bpc = 8u};
    return src;
}

/* 576x324 NV12: linear extent 161 * 640 + 576; tiled 6 tile rows * 5 tiles * 4 KiB. */
#define LINEAR_OBJECT 103616u
#define TILED_OBJECT (225280u + 122880u)

static char *test_validate_accepts_and_bounds(void)
{
    VmafSyclChromaSrc src = valid_src(VMAF_SYCL_CHROMA_LINEAR);
    mu_assert("valid linear plane", !vmaf_sycl_chroma_src_validate(&src, LINEAR_OBJECT));
    mu_assert("linear extent one byte past the object",
              vmaf_sycl_chroma_src_validate(&src, LINEAR_OBJECT - 1u) == -EINVAL);
    for (unsigned i = 1u; i < N_LAYOUTS; i++) {
        src = valid_src(g_layouts[i]);
        mu_assert("valid tiled plane", !vmaf_sycl_chroma_src_validate(&src, TILED_OBJECT));
        mu_assert("tiled extent one byte past the object",
                  vmaf_sycl_chroma_src_validate(&src, TILED_OBJECT - 1u) == -EINVAL);
        src.pitch = 600u;
        mu_assert("tiled pitch must be a multiple of 128",
                  vmaf_sycl_chroma_src_validate(&src, TILED_OBJECT) == -EINVAL);
    }
    src = valid_src(VMAF_SYCL_CHROMA_LINEAR);
    src.cw = 400u;
    mu_assert("a row of pairs wider than the pitch",
              vmaf_sycl_chroma_src_validate(&src, LINEAR_OBJECT * 4u) == -EINVAL);
    return NULL;
}

static char *test_validate_rejects_malformed(void)
{
    VmafSyclChromaSrc src = valid_src(VMAF_SYCL_CHROMA_LINEAR);
    mu_assert("NULL src", vmaf_sycl_chroma_src_validate(NULL, LINEAR_OBJECT) == -EINVAL);
    src.base = NULL;
    mu_assert("NULL base", vmaf_sycl_chroma_src_validate(&src, LINEAR_OBJECT) == -EINVAL);
    src = valid_src(VMAF_SYCL_CHROMA_LINEAR);
    src.cw = 0u;
    mu_assert("zero width", vmaf_sycl_chroma_src_validate(&src, LINEAR_OBJECT) == -EINVAL);
    src = valid_src(VMAF_SYCL_CHROMA_LINEAR);
    src.ch = 0u;
    mu_assert("zero height", vmaf_sycl_chroma_src_validate(&src, LINEAR_OBJECT) == -EINVAL);
    const unsigned bad_bpc[] = {0u, 7u, 9u, 11u, 14u, 32u};
    for (unsigned i = 0u; i < sizeof(bad_bpc) / sizeof(bad_bpc[0]); i++) {
        src = valid_src(VMAF_SYCL_CHROMA_LINEAR);
        src.bpc = bad_bpc[i];
        mu_assert("bit depth outside 8/10/12/16",
                  vmaf_sycl_chroma_src_validate(&src, LINEAR_OBJECT * 4u) == -EINVAL);
    }
    src = valid_src(VMAF_SYCL_CHROMA_LINEAR);
    src.layout = (enum VmafSyclChromaLayout)99;
    mu_assert("unknown layout", vmaf_sycl_chroma_src_validate(&src, LINEAR_OBJECT) == -EINVAL);
    return NULL;
}

static char *test_validate_extent_is_size_t(void)
{
    /* 32-bit arithmetic would wrap (ch-1) * pitch = 2^32 to 0 and accept this. */
    VmafSyclChromaSrc src = valid_src(VMAF_SYCL_CHROMA_LINEAR);
    src.offset = 0u;
    src.cw = 1u;
    src.ch = 0x10001u;
    src.pitch = 0x10000u;
    mu_assert("a 2^32-byte extent in a 1 MiB object must be rejected",
              vmaf_sycl_chroma_src_validate(&src, (size_t)1u << 20) == -EINVAL);
    if (sizeof(size_t) >= 8u) {
        mu_assert("the same plane inside an object that holds it is valid",
                  !vmaf_sycl_chroma_src_validate(&src, (size_t)1u << 33));
    }
    src = valid_src(VMAF_SYCL_CHROMA_LINEAR);
    src.offset = SIZE_MAX - 4u;
    mu_assert("an offset that wraps size_t must be rejected",
              vmaf_sycl_chroma_src_validate(&src, LINEAR_OBJECT) == -EINVAL);
    src = valid_src(VMAF_SYCL_CHROMA_TILE4);
    src.offset = SIZE_MAX - 4096u;
    mu_assert("a tiled offset that wraps size_t must be rejected",
              vmaf_sycl_chroma_src_validate(&src, TILED_OBJECT) == -EINVAL);
    src = valid_src(VMAF_SYCL_CHROMA_YTILED);
    src.offset = 0u;
    src.ch = 0xFFFFFFFFu;
    src.pitch = 0xFFFFFF80u;
    mu_assert("a huge tiled plane in a small object must be rejected",
              vmaf_sycl_chroma_src_validate(&src, TILED_OBJECT) == -EINVAL);
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_modifier_mapping);
    mu_run_test(test_validate_accepts_and_bounds);
    mu_run_test(test_validate_rejects_malformed);
    mu_run_test(test_validate_extent_is_size_t);
    mu_run_test(test_kernel_matches_reference);
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
