/**
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: EUPL-1.2
 *
 *  Layout-addressed chroma de-interleave kernel for the SYCL zero-copy import
 *  (ADR-1765). No libva dependency: the kernel reads a device pointer, so the
 *  address math is unit-tested without a decoder (test_sycl_chroma_import).
 */

#ifndef VMAF_SRC_SYCL_CHROMA_IMPORT_H
#define VMAF_SRC_SYCL_CHROMA_IMPORT_H

/* NOLINTBEGIN(modernize-use-using, performance-enum-size):
 * internal header shared by the SYCL C++ sources and the C unit test
 * core/test/test_sycl_chroma_import.c. clang-tidy reads it as C++ and
 * proposes `using` and a `std::uint8_t` enum base type; both are wrong here.
 * This header has to compile as C, where `using` and `std::` do not exist and
 * the c17 fallback of the build's C standard list has no fixed enum base
 * type; the enum is a field of VmafSyclChromaSrc, so its size must be the
 * same in the C and the C++ translation units. CLAUDE.md rule 12 reserves
 * suppressions for exactly this: a rule that cannot be followed without
 * breaking a load-bearing invariant (ADR-0141). */

#include <stddef.h>
#include <stdint.h>

#include "config.h"

#if HAVE_SYCL

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Memory layout of an interleaved UV plane (a VA surface's `layers[1]`). */
enum VmafSyclChromaLayout {
    VMAF_SYCL_CHROMA_LINEAR, /**< Row-major, `pitch` bytes per row. */
    VMAF_SYCL_CHROMA_TILE4,  /**< I915_FORMAT_MOD_4_TILED: 128 B x 32 row tiles. */
    VMAF_SYCL_CHROMA_YTILED, /**< I915_FORMAT_MOD_Y_TILED: 128 B x 32 row tiles. */
};

/**
 * One interleaved UV plane to convert (NV12 or P010 style, 4:2:0).
 *
 * `pitch` and `offset` are in bytes. `cw` and `ch` are the chroma plane's
 * width and height in samples, `(w+1)/2` and `(h+1)/2` of the luma frame. A
 * sample pair is `2 * ((bpc + 7) / 8)` bytes, U first.
 */
typedef struct VmafSyclChromaSrc {
    const void *base;                 /**< Device USM pointer to the whole object. */
    enum VmafSyclChromaLayout layout; /**< Layout of the plane. */
    size_t offset;                    /**< Byte offset of the plane inside the object. */
    unsigned pitch;                   /**< Bytes per row (linear) or per tile row (tiled). */
    unsigned cw;                      /**< Chroma width in samples, non-zero. */
    unsigned ch;                      /**< Chroma height in samples, non-zero. */
    unsigned bpc;                     /**< Luma bit depth: 8, 10, 12 or 16. */
} VmafSyclChromaSrc;

/**
 * Map a DRM format modifier to a chroma layout (ADR-1765).
 *
 * @param modifier  DRM format modifier of the surface's object.
 * @param[out] out  Receives the layout on success.
 *
 * @return 0 for LINEAR (0), Tile4 and Y-tiled; -ENOTSUP for any other modifier
 *         (the caller takes the readback path); -EINVAL when `out` is NULL.
 */
int vmaf_sycl_chroma_layout_from_modifier(uint64_t modifier, enum VmafSyclChromaLayout *out);

/**
 * Check a chroma plane descriptor before any device access (T-12-09, T-12-10).
 *
 * All extent math is in `size_t`. LINEAR requires
 * `offset + (ch-1)*pitch + cw*2*bps <= object_size`; tiled layouts require
 * `pitch % 128 == 0` and `offset + ceil(ch/32) * (pitch/128) * 4096 <=
 * object_size`. In every layout a row of pairs (`cw*2*bps`) must fit in `pitch`.
 *
 * @param src          The plane to check.
 * @param object_size  Size in bytes of the object `src->base` points to.
 *
 * @return 0 when valid; -EINVAL for a NULL `src` or `base`, zero `cw`/`ch`,
 *         `bpc` not in {8, 10, 12, 16}, a bad layout or pitch, or an extent
 *         beyond `object_size`.
 */
int vmaf_sycl_chroma_src_validate(const VmafSyclChromaSrc *src, size_t object_size);

/**
 * De-interleave a UV plane into planar Cb and Cr (ADR-1765).
 *
 * One work-item per chroma sample. Writes `cb[y*cw+x]` and `cr[y*cw+x]` with a
 * tight pitch, as `uint8_t` for `bpc == 8` and `uint16_t` otherwise. For
 * `bpc > 8` the MSB-aligned (P010-style) samples are shifted right by
 * `16 - bpc` here, exactly once; the caller must not normalise the planes
 * again. The queue is the state's primary queue (in-order).
 *
 * @param state          The SYCL state.
 * @param src            Plane to convert; checked by vmaf_sycl_chroma_src_validate()
 *                       against `object_size`.
 * @param object_size    Size in bytes of the object `src->base` points to.
 * @param dst_cb         Device pointer for the Cb plane, `cw*ch` samples.
 * @param dst_cr         Device pointer for the Cr plane, `cw*ch` samples.
 * @param[out] out_event NULL: the call returns after the kernel finished.
 *                       Non-NULL: the kernel is only submitted and
 *                       `*out_event` receives a heap-allocated `sycl::event`
 *                       that the caller owns. It passes it to
 *                       vmaf_sycl_set_detile_event() (which copies it) and then
 *                       releases it with vmaf_sycl_chroma_event_free().
 *
 * @return 0 on success; the validation error; -EINVAL for NULL arguments;
 *         -EIO when the device threw; -ENOMEM when the event allocation failed.
 */
int vmaf_sycl_chroma_import_launch(VmafSyclState *state, const VmafSyclChromaSrc *src,
                                   size_t object_size, void *dst_cb, void *dst_cr,
                                   void **out_event);

/**
 * Release an event returned through `out_event` by
 * vmaf_sycl_chroma_import_launch().
 *
 * @param event  The pointer, or NULL.
 */
void vmaf_sycl_chroma_event_free(void *event);

#ifdef __cplusplus
}
#endif

#endif /* HAVE_SYCL */

/* NOLINTEND(modernize-use-using, performance-enum-size) */

#endif /* VMAF_SRC_SYCL_CHROMA_IMPORT_H */
