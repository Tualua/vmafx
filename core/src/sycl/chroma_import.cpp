/**
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: EUPL-1.2
 *
 *  Layout-addressed chroma de-interleave kernel for the SYCL zero-copy import
 *  (ADR-1765). One work-item converts one UV pair of a LINEAR, Tile4 or
 *  Y-tiled plane into one Cb and one Cr sample. Integer only, no private
 *  arrays (ADR-1395, ADR-0220). This TU includes no libva header, so the
 *  address math is testable without a decoder.
 */
#include "config.h"

#if HAVE_SYCL

#include <cerrno>
#include <cstdint>
#include <new>

#include <sycl/sycl.hpp>

#include "chroma_import.h"
#include "log.h"

namespace
{

/* DRM format modifiers of the surfaces the VA import sees. The luma path in
 * dmabuf_import.cpp spells the same values behind libva's headers. */
constexpr uint64_t kModLinear = 0ULL;
constexpr uint64_t I915_FORMAT_MOD_4_TILED = 0x0100000000000009ULL;
constexpr uint64_t I915_FORMAT_MOD_Y_TILED = 0x0100000000000002ULL;

constexpr size_t kTileBytes = 4096u; /* 128 bytes x 32 rows */
constexpr unsigned kTileRowBytes = 128u;

/* Checked size_t arithmetic: descriptor fields are driver data (T-12-10). */
bool mul_size(size_t a, size_t b, size_t *out)
{
    if (a != 0u && b > SIZE_MAX / a)
        return false;
    *out = a * b;
    return true;
}

bool add_size(size_t a, size_t b, size_t *out)
{
    if (b > SIZE_MAX - a)
        return false;
    *out = a + b;
    return true;
}

bool bpc_supported(unsigned bpc)
{
    return bpc == 8u || bpc == 10u || bpc == 12u || bpc == 16u;
}

/* Bytes the plane spans from its offset, or false when it overflows size_t. */
bool plane_extent(const VmafSyclChromaSrc *src, size_t row_bytes, size_t *extent)
{
    if (src->layout == VMAF_SYCL_CHROMA_LINEAR) {
        size_t above = 0u;
        return mul_size((size_t)src->ch - 1u, src->pitch, &above) &&
               add_size(above, row_bytes, extent);
    }
    size_t tiles = 0u;
    return mul_size(((size_t)src->ch + 31u) / 32u, src->pitch / kTileRowBytes, &tiles) &&
           mul_size(tiles, kTileBytes, extent);
}

/* Tile4: bit-interleave of the byte column x (0..127) and the row y (0..31),
 * the same swizzle the luma de-tile kernel applies (dmabuf_import.cpp). */
inline unsigned tile4_offset(unsigned x, unsigned y)
{
    return (x & 0x0Fu) | ((y & 3u) << 4) | (((x >> 4) & 3u) << 6) | (((y >> 2) & 1u) << 8) |
           (((x >> 6) & 1u) << 9) | (((y >> 3) & 1u) << 10) | (((y >> 4) & 1u) << 11);
}

/* Y-tiled: column-major 16-byte OWords, 512 bytes per OWord column. */
inline unsigned ytile_offset(unsigned x, unsigned y)
{
    return ((x >> 4) << 9) | (y << 4) | (x & 0x0Fu);
}

struct ChromaKernelArgs {
    const uint8_t *src;
    uint8_t *cb;
    uint8_t *cr;
    size_t offset;
    unsigned pitch;
    unsigned cw;
    unsigned bps;
    unsigned shift;
    unsigned layout;
};

inline size_t pair_address(const ChromaKernelArgs &a, unsigned xb, unsigned y)
{
    if (a.layout == VMAF_SYCL_CHROMA_LINEAR)
        return a.offset + (size_t)y * a.pitch + xb;
    const size_t tiles_per_row = a.pitch / kTileRowBytes;
    const size_t tile = ((size_t)(y >> 5) * tiles_per_row + (xb >> 7)) * kTileBytes;
    const unsigned xt = xb & 127u;
    const unsigned yt = y & 31u;
    return a.offset + tile +
           (a.layout == VMAF_SYCL_CHROMA_TILE4 ? tile4_offset(xt, yt) : ytile_offset(xt, yt));
}

inline unsigned load_sample(const uint8_t *p, unsigned bps)
{
    return bps == 1u ? (unsigned)p[0] : ((unsigned)p[0] | ((unsigned)p[1] << 8));
}

inline void store_sample(uint8_t *p, unsigned bps, unsigned value)
{
    p[0] = (uint8_t)(value & 0xFFu);
    if (bps == 2u)
        p[1] = (uint8_t)(value >> 8);
}

/* One UV pair -> one Cb and one Cr sample; the P010 shift happens here, once. */
inline void convert_pair(const ChromaKernelArgs &a, unsigned x, unsigned y)
{
    const uint8_t *const pair = a.src + pair_address(a, x * 2u * a.bps, y);
    const unsigned u = load_sample(pair, a.bps) >> a.shift;
    const unsigned v = load_sample(pair + a.bps, a.bps) >> a.shift;
    const size_t at = ((size_t)y * a.cw + x) * a.bps;
    store_sample(a.cb + at, a.bps, u);
    store_sample(a.cr + at, a.bps, v);
}

/* Wait for the kernel, or hand its event to the caller. */
int finish(sycl::event ev, void **out_event)
{
    if (!out_event) {
        ev.wait_and_throw();
        return 0;
    }
    *out_event = new (std::nothrow) sycl::event(ev);
    if (!*out_event) {
        ev.wait_and_throw();
        return -ENOMEM;
    }
    return 0;
}

} /* namespace */

extern "C" int vmaf_sycl_chroma_layout_from_modifier(uint64_t modifier,
                                                     enum VmafSyclChromaLayout *out)
{
    if (!out)
        return -EINVAL;
    if (modifier == kModLinear) {
        *out = VMAF_SYCL_CHROMA_LINEAR;
    } else if (modifier == I915_FORMAT_MOD_4_TILED) {
        *out = VMAF_SYCL_CHROMA_TILE4;
    } else if (modifier == I915_FORMAT_MOD_Y_TILED) {
        *out = VMAF_SYCL_CHROMA_YTILED;
    } else {
        return -ENOTSUP;
    }
    return 0;
}

extern "C" int vmaf_sycl_chroma_src_validate(const VmafSyclChromaSrc *src, size_t object_size)
{
    if (!src || !src->base || src->cw == 0u || src->ch == 0u || !bpc_supported(src->bpc))
        return -EINVAL;
    const size_t row_bytes = (size_t)src->cw * 2u * ((src->bpc + 7u) / 8u);
    if (row_bytes > src->pitch)
        return -EINVAL;
    if (src->layout != VMAF_SYCL_CHROMA_LINEAR && src->layout != VMAF_SYCL_CHROMA_TILE4 &&
        src->layout != VMAF_SYCL_CHROMA_YTILED)
        return -EINVAL;
    if (src->layout != VMAF_SYCL_CHROMA_LINEAR && src->pitch % kTileRowBytes != 0u)
        return -EINVAL;
    size_t extent = 0u;
    size_t end = 0u;
    if (!plane_extent(src, row_bytes, &extent) || !add_size(src->offset, extent, &end))
        return -EINVAL;
    return end <= object_size ? 0 : -EINVAL;
}

extern "C" int vmaf_sycl_chroma_import_launch(VmafSyclState *state, const VmafSyclChromaSrc *src,
                                              size_t object_size, void *dst_cb, void *dst_cr,
                                              void **out_event)
{
    if (!state || !src || !dst_cb || !dst_cr)
        return -EINVAL;
    const int err = vmaf_sycl_chroma_src_validate(src, object_size);
    if (err)
        return err;
    sycl::queue *const q = static_cast<sycl::queue *>(vmaf_sycl_get_queue_ptr(state));
    if (!q)
        return -EINVAL;
    const ChromaKernelArgs args = {.src = static_cast<const uint8_t *>(src->base),
                                   .cb = static_cast<uint8_t *>(dst_cb),
                                   .cr = static_cast<uint8_t *>(dst_cr),
                                   .offset = src->offset,
                                   .pitch = src->pitch,
                                   .cw = src->cw,
                                   .bps = (src->bpc + 7u) / 8u,
                                   .shift = src->bpc > 8u ? 16u - src->bpc : 0u,
                                   .layout = (unsigned)src->layout};
    try {
        const sycl::event ev =
            q->parallel_for(sycl::range<2>(src->ch, src->cw), [=](sycl::id<2> id) {
                convert_pair(args, (unsigned)id[1], (unsigned)id[0]);
            });
        return finish(ev, out_event);
    } catch (const sycl::exception &e) {
        vmaf_log(VMAF_LOG_LEVEL_ERROR, "chroma import kernel failed: %s\n", e.what());
        return -EIO;
    }
}

extern "C" void vmaf_sycl_chroma_event_free(void *event)
{
    delete static_cast<sycl::event *>(event);
}

#endif /* HAVE_SYCL */
