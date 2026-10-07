/**
 *
 *  Copyright 2026 Lusoris
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 *     Licensed under the BSD+Patent License (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         https://opensource.org/licenses/BSDplusPatent
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * SYCL VA-API surface import — import VA-API decoded frames into SYCL
 * shared frame buffers for zero-host-copy VMAF computation.
 *
 * Primary (zero-copy) path:
 *   VA surface → vaExportSurfaceHandle (DRM PRIME2) → DMA-BUF fd
 *   → Level Zero external memory import → SYCL device pointer
 *   → SYCL de-tiling kernel (Tile4/Y-tiled → linear) or D2D memcpy (LINEAR)
 *   → shared frame buffer.
 *   Everything stays on GPU — no CPU copies, no PCIe pixel traffic.
 *
 * Fallback (readback) path:
 *   VA surface → vaGetImage (de-tiles) → vaMapBuffer → H2D memcpy.
 *   Used when vaExportSurfaceHandle or DMA-BUF import fails.
 *
 * DMA-BUF import helpers (vmaf_sycl_dmabuf_import/free):
 *   Raw DMA-BUF fd → Level Zero zeMemAllocDevice → SYCL device pointer.
 */

#include "config.h"

#if HAVE_SYCL

/* DMA-BUF is a Linux kernel concept (O_CLOEXEC fd → GPU device memory import
 * via ZE_EXTERNAL_MEMORY_TYPE_FLAG_DMA_BUF). Level Zero on Windows uses
 * ze_external_memory_import_win32_handle_t / NT handles instead; the DMA-BUF
 * path therefore cannot compile or run on Windows. Guard the entire body of
 * this TU (all three functions: dmabuf_import, dmabuf_free, import_va_surface)
 * with #ifndef _WIN32 and provide -ENOSYS stubs at the bottom so the linker
 * is satisfied on Windows SYCL builds.
 *
 * The HAVE_SYCL_DMABUF config flag guards the VA-API / unistd.h path already;
 * this _WIN32 guard is the outer layer that prevents level_zero/ze_api.h DMA-BUF
 * types and the int-fd argument from reaching a Windows compiler. */
#ifndef _WIN32

#include <cassert>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstring>

#include <sycl/sycl.hpp>
#include <sycl/backend.hpp>
#include <level_zero/ze_api.h>

#if HAVE_SYCL_DMABUF
/* unistd.h (POSIX close()) only used inside the VA-API import path
 * below — guard alongside libva so non-DMA-BUF builds (macOS,
 * Linux without VA-API) don't fail with "unistd.h not found". */
#include <unistd.h>
#include <va/va.h>
#include <va/va_drmcommon.h>
#endif

#include "dmabuf_import.h"
#include "chroma_import.h"
#include "common.h"
#include "log.h"

/* ------------------------------------------------------------------ */
/* DMA-BUF → Level Zero → SYCL device pointer                         */
/* ------------------------------------------------------------------ */

extern "C" int vmaf_sycl_dmabuf_import(VmafSyclState *state, int fd, size_t size, void **ptr)
{
    if (!state || fd < 0 || !size || !ptr)
        return -EINVAL;
    *ptr = nullptr;

    try {
        const sycl::queue *q = static_cast<sycl::queue *>(vmaf_sycl_get_queue_ptr(state));
        if (!q)
            return -EINVAL;

        /* Extract Level Zero native handles from the SYCL queue */
        ze_context_handle_t ze_ctx =
            sycl::get_native<sycl::backend::ext_oneapi_level_zero>(q->get_context());
        ze_device_handle_t ze_dev =
            sycl::get_native<sycl::backend::ext_oneapi_level_zero>(q->get_device());

        /* DMA-BUF import descriptor, chained into the device allocation
         * descriptor. Designated initialisers: the structure type is valid
         * from the first moment and every field not named here is zero, also
         * one a newer Level Zero header adds. */
        const ze_external_memory_import_fd_t import_desc = {
            .stype = ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMPORT_FD,
            .pNext = nullptr,
            .flags = ZE_EXTERNAL_MEMORY_TYPE_FLAG_DMA_BUF,
            .fd = fd,
        };
        const ze_device_mem_alloc_desc_t alloc_desc = {
            .stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC,
            .pNext = &import_desc,
            .flags = 0,
            .ordinal = 0,
        };

        void *ze_ptr = nullptr;
        const ze_result_t res =
            zeMemAllocDevice(ze_ctx, &alloc_desc, size, 0 /* alignment */, ze_dev, &ze_ptr);
        if (res != ZE_RESULT_SUCCESS) {
            vmaf_log(VMAF_LOG_LEVEL_ERROR, "Level Zero DMA-BUF import failed: 0x%x\n", res);
            return -EIO;
        }

        *ptr = ze_ptr;
        return 0;

    } catch (const sycl::exception &e) {
        vmaf_log(VMAF_LOG_LEVEL_ERROR, "SYCL DMA-BUF import exception: %s\n", e.what());
        return -EIO;
    } catch (const std::exception &e) {
        vmaf_log(VMAF_LOG_LEVEL_ERROR, "DMA-BUF import error: %s\n", e.what());
        return -EIO;
    }
}

extern "C" void vmaf_sycl_dmabuf_free(VmafSyclState *state, void *ptr)
{
    if (!state || !ptr)
        return;

    try {
        const sycl::queue *q = static_cast<sycl::queue *>(vmaf_sycl_get_queue_ptr(state));
        if (!q)
            return;

        ze_context_handle_t ze_ctx =
            sycl::get_native<sycl::backend::ext_oneapi_level_zero>(q->get_context());

        zeMemFree(ze_ctx, ptr);
    } catch (...) {
        /* Best effort — if SYCL/L0 is already torn down, ignore. Logged at DEBUG
         * so the catch is not empty (bugprone-empty-catch) and teardown faults
         * remain visible under VMAF_LOG_LEVEL_DEBUG without affecting the path. */
        vmaf_log(VMAF_LOG_LEVEL_DEBUG,
                 "vmaf_sycl_dmabuf_free: ignoring exception during teardown\n");
    }
}

#if HAVE_SYCL_DMABUF

/* ------------------------------------------------------------------ */
/* DRM format modifier constants                                       */
/* ------------------------------------------------------------------ */

#ifndef DRM_FORMAT_MOD_LINEAR
#define DRM_FORMAT_MOD_LINEAR 0ULL
#endif

/* fourcc_mod_code(INTEL, 9) */
#ifndef I915_FORMAT_MOD_4_TILED
#define I915_FORMAT_MOD_4_TILED 0x0100000000000009ULL
#endif

/* fourcc_mod_code(INTEL, 2) */
#ifndef I915_FORMAT_MOD_Y_TILED
#define I915_FORMAT_MOD_Y_TILED 0x0100000000000002ULL
#endif

/* fourcc_mod_code(INTEL, 1) */
#ifndef I915_FORMAT_MOD_X_TILED
#define I915_FORMAT_MOD_X_TILED 0x0100000000000001ULL
#endif

/* ------------------------------------------------------------------ */
/* P010 / P012 MSB-aligned → LSB-aligned pixel normalization           */
/* ------------------------------------------------------------------ */
/*
 * VA-API encodes 10-bit (P010) or 12-bit (P012) luma/chroma as MSB-aligned
 * uint16_t values: V_MSB = V_LSB << (16 - bpc).  The 10-bit studio-swing
 * range [64..940] becomes [4096..60160] in P010.
 *
 * The upstream VMAF feature kernels (integer_motion, VIF, ADM) were designed
 * for LSB-aligned bpc-bit integers (range [0, 2^bpc - 1]).  The CPU libvmaf
 * path receives LSB-aligned data because FFmpeg automatically converts
 * AV_PIX_FMT_P010LE → AV_PIX_FMT_YUV420P10LE (which shifts right by 6) to
 * satisfy libvmaf's FILTER_PIXFMTS list (which does not include P010LE).
 *
 * This standalone normalization is used by the import paths that do NOT detile
 * with a per-sample kernel — the readback memcpy and the DMA-BUF LINEAR D2D
 * memcpy — where there is no store to fuse the shift into. The Tile4 / Y-tiled
 * de-tile kernels instead apply the `>> (16 - bpc)` shift inline as they write
 * each sample (no extra kernel launch, no second memory pass). This function
 * submits a SYCL kernel that right-shifts every uint16_t in `buf` by
 * `(16 - bpc)` bits in-place and returns the event.
 *
 * Callers MUST guard with `if (bpc > 8)`; calling this function for 8-bit NV12
 * surfaces is undefined behaviour — the uint8_t pixels are already LSB-aligned,
 * and reinterpreting that buffer as uint16_t then shifting would corrupt data.
 * The `assert(bpc > 8 && bpc < 16)` below brackets the supported range (P010
 * bpc=10, P012 bpc=12); a shift of >= 16 on a uint16_t is itself UB.
 *
 * Callers must chain: the returned event is submitted to the same in-order
 * queue that ran the preceding memcpy / de-tile kernel.  Because the queue is
 * in-order, the normalization kernel automatically runs after all prior work.
 * The caller should pass this event to vmaf_sycl_set_detile_event() so that
 * the compute-barrier waits for normalization to complete before any extractor
 * kernel reads the shared frame buffer.
 */
namespace
{

sycl::event launch_p010_normalize(sycl::queue *q, void *buf, unsigned w, unsigned h, unsigned bpc)
{
    assert(bpc > 8 && bpc < 16);
    unsigned const shift = 16u - bpc; /* 6 for bpc=10; 4 for bpc=12 */
    size_t const num_pixels = (size_t)w * h;
    auto *pixels = static_cast<uint16_t *>(buf);

    return q->parallel_for(sycl::range<1>(num_pixels),
                           [=](sycl::id<1> id) { pixels[id[0]] >>= shift; });
}

int find_va_format(VADisplay va_dpy, unsigned bpc, VAImageFormat *out_fmt)
{
    assert(out_fmt != nullptr);
    int const num_fmts = vaMaxNumImageFormats(va_dpy);
    if (num_fmts <= 0) {
        vmaf_log(VMAF_LOG_LEVEL_ERROR, "vaMaxNumImageFormats returned %d\n", num_fmts);
        return -EIO;
    }
    auto *fmts = static_cast<VAImageFormat *>(malloc((size_t)num_fmts * sizeof(VAImageFormat)));
    if (!fmts)
        return -ENOMEM;
    int actual = 0;
    VAStatus const qst = vaQueryImageFormats(va_dpy, fmts, &actual);
    if (qst != VA_STATUS_SUCCESS) {
        free(fmts);
        vmaf_log(VMAF_LOG_LEVEL_ERROR, "vaQueryImageFormats failed: %s\n", vaErrorStr(qst));
        return -EIO;
    }

    uint32_t const target_fourcc = (bpc <= 8) ? VA_FOURCC_NV12 : VA_FOURCC_P010;
    bool found = false;
    for (int i = 0; i < actual; i++) {
        if (fmts[i].fourcc == target_fourcc) {
            *out_fmt = fmts[i];
            found = true;
            break;
        }
    }
    free(fmts);

    if (!found) {
        vmaf_log(VMAF_LOG_LEVEL_ERROR, "VA image format %s not supported\n",
                 bpc <= 8 ? "NV12" : "P010");
        return -ENOTSUP;
    }
    return 0;
}

int copy_and_normalize_readback(sycl::queue *q, void *target_buf, const uint8_t *y_plane,
                                uint32_t y_pitch, size_t y_row_bytes, unsigned w, unsigned h,
                                unsigned bpc)
{
    try {
        if (y_pitch == y_row_bytes) {
            q->memcpy(target_buf, y_plane, y_row_bytes * h);
        } else {
            const uint8_t *src = y_plane;
            auto *dst = static_cast<uint8_t *>(target_buf);
            for (unsigned row = 0; row < h; row++) {
                q->memcpy(dst, src, y_row_bytes);
                src += y_pitch;
                dst += y_row_bytes;
            }
        }
        if (bpc > 8)
            (void)launch_p010_normalize(q, target_buf, w, h, bpc);
        q->wait_and_throw();
        return 0;
    } catch (const sycl::exception &e) {
        vmaf_log(VMAF_LOG_LEVEL_ERROR, "vmaf_sycl readback memcpy failed: %s\n", e.what());
        return -EIO;
    }
}

/* ------------------------------------------------------------------ */
/* Chroma (layers[1]) import helpers, ADR-1597                         */
/* ------------------------------------------------------------------ */

/* Where the interleaved UV plane of a PRIME2 descriptor sits. */
struct VaChromaPlan {
    VmafSyclChromaSrc src; /* base is filled in once the object is imported */
    uint32_t obj;          /* index of the object holding the plane */
    size_t obj_size;       /* that object's size in bytes */
};

/* Resolve layers[1] of `desc`. -EINVAL: no second layer or an object index
 * past num_objects (T-12-11); -ENOTSUP: modifier without a known chroma
 * layout (the caller takes the readback path). The extent against the
 * object's size is checked by the launch (vmaf_sycl_chroma_src_validate). */
int va_chroma_plan(const VADRMPRIMESurfaceDescriptor &desc, unsigned w, unsigned h, unsigned bpc,
                   VaChromaPlan *plan)
{
    if (desc.num_layers < 2 || desc.layers[1].object_index[0] >= desc.num_objects)
        return -EINVAL;
    uint32_t const obj = desc.layers[1].object_index[0];
    enum VmafSyclChromaLayout layout;
    int const lerr =
        vmaf_sycl_chroma_layout_from_modifier(desc.objects[obj].drm_format_modifier, &layout);
    if (lerr)
        return lerr;
    plan->obj = obj;
    plan->obj_size = desc.objects[obj].size;
    plan->src = VmafSyclChromaSrc{};
    plan->src.layout = layout;
    plan->src.offset = desc.layers[1].offset[0];
    plan->src.pitch = desc.layers[1].pitch[0];
    plan->src.cw = (w + 1) / 2;
    plan->src.ch = (h + 1) / 2;
    plan->src.bpc = bpc;
    return 0;
}

/* De-interleave `src` into the upload slot's Cb / Cr planes of this side.
 * Submitted behind the luma de-tile on the in-order primary queue; the event
 * replaces the luma one in set_detile_event, so it covers both. `wait` makes
 * the call return after the kernel finished (readback temporary). No P010
 * normalisation here: the kernel shifts once. */
int va_chroma_launch(VmafSyclState *state, const VmafSyclChromaSrc &src, size_t object_size,
                     int is_ref, bool wait)
{
    void *const dst_cb = vmaf_sycl_get_shared_plane_upload(state, is_ref, 1);
    void *const dst_cr = vmaf_sycl_get_shared_plane_upload(state, is_ref, 2);
    if (!dst_cb || !dst_cr) {
        vmaf_log(VMAF_LOG_LEVEL_ERROR, "Shared chroma planes not initialised\n");
        return -EINVAL;
    }
    if (wait)
        return vmaf_sycl_chroma_import_launch(state, &src, object_size, dst_cb, dst_cr, nullptr);
    void *ev = nullptr;
    int const err = vmaf_sycl_chroma_import_launch(state, &src, object_size, dst_cb, dst_cr, &ev);
    if (err)
        return err;
    vmaf_sycl_set_detile_event(state, ev);
    vmaf_sycl_chroma_event_free(ev);
    return 0;
}

/* Readback: copy the VAImage's interleaved UV plane to a temporary device
 * buffer and de-interleave it as a LINEAR plane (synchronously). */
int va_readback_chroma(VmafSyclState *state, const VAImage &img, const void *img_data, int is_ref,
                       unsigned w, unsigned h, unsigned bpc)
{
    if (img.num_planes < 2)
        return -ENOTSUP;
    VmafSyclChromaSrc src = {};
    src.layout = VMAF_SYCL_CHROMA_LINEAR;
    src.pitch = img.pitches[1];
    src.cw = (w + 1) / 2;
    src.ch = (h + 1) / 2;
    src.bpc = bpc;
    size_t const row_bytes = static_cast<size_t>(src.cw) * 2U * ((bpc + 7U) / 8U);
    size_t const bytes = static_cast<size_t>(src.ch - 1) * src.pitch + row_bytes;
    if (src.pitch < row_bytes || static_cast<size_t>(img.offsets[1]) + bytes > img.data_size)
        return -EINVAL;
    void *const dev = vmaf_sycl_malloc_device(state, bytes);
    if (!dev)
        return -ENOMEM;
    src.base = dev;
    int err = vmaf_sycl_memcpy_h2d(state, dev,
                                   static_cast<const uint8_t *>(img_data) + img.offsets[1], bytes);
    if (!err)
        err = va_chroma_launch(state, src, bytes, is_ref, true);
    vmaf_sycl_free(state, dev);
    return err;
}

/* Create a VAImage of the surface in `fmt`, copy the surface into it and map
 * its buffer. On success the caller unmaps `img->buf` and destroys
 * `img->image_id`; on failure nothing is left to release. */
int va_readback_map_image(VADisplay va_dpy, VASurfaceID va_surf, VAImageFormat *fmt, unsigned w,
                          unsigned h, VAImage *img, void **data)
{
    VAStatus va_st = vaCreateImage(va_dpy, fmt, w, h, img);
    if (va_st != VA_STATUS_SUCCESS) {
        vmaf_log(VMAF_LOG_LEVEL_ERROR, "vaCreateImage failed: %s\n", vaErrorStr(va_st));
        return -EIO;
    }

    va_st = vaGetImage(va_dpy, va_surf, 0, 0, w, h, img->image_id);
    if (va_st != VA_STATUS_SUCCESS) {
        vmaf_log(VMAF_LOG_LEVEL_ERROR, "vaGetImage failed: %s\n", vaErrorStr(va_st));
        vaDestroyImage(va_dpy, img->image_id);
        return -EIO;
    }

    va_st = vaMapBuffer(va_dpy, img->buf, data);
    if (va_st != VA_STATUS_SUCCESS) {
        vmaf_log(VMAF_LOG_LEVEL_ERROR, "vaMapBuffer failed: %s\n", vaErrorStr(va_st));
        vaDestroyImage(va_dpy, img->image_id);
        return -EIO;
    }
    return 0;
}

int vmaf_sycl_import_va_surface_readback(VmafSyclState *state, void *va_display_handle,
                                         unsigned int va_surface_id, int is_ref, unsigned w,
                                         unsigned h, unsigned bpc)
{
    auto va_dpy = static_cast<VADisplay>(va_display_handle);
    auto const va_surf = static_cast<VASurfaceID>(va_surface_id);
    unsigned const bytes_per_pixel = (bpc + 7) / 8;
    VAImageFormat y_fmt = {};

    int rc = find_va_format(va_dpy, bpc, &y_fmt);
    if (rc != 0)
        return rc;

    VAImage va_img = {};
    void *img_data = nullptr;
    rc = va_readback_map_image(va_dpy, va_surf, &y_fmt, w, h, &va_img, &img_data);
    if (rc != 0)
        return rc;

    const auto *y_plane = static_cast<const uint8_t *>(img_data) + va_img.offsets[0];
    uint32_t const y_pitch = va_img.pitches[0];
    size_t const y_row_bytes = static_cast<size_t>(w) * bytes_per_pixel;

    void *target_buf =
        is_ref ? vmaf_sycl_get_shared_ref_upload(state) : vmaf_sycl_get_shared_dis_upload(state);

    if (!target_buf) {
        vaUnmapBuffer(va_dpy, va_img.buf);
        vaDestroyImage(va_dpy, va_img.image_id);
        return -EINVAL;
    }

    auto *q = static_cast<sycl::queue *>(vmaf_sycl_get_queue_ptr(state));
    rc = copy_and_normalize_readback(q, target_buf, y_plane, y_pitch, y_row_bytes, w, h, bpc);
    int const cerr = rc ? 0 : va_readback_chroma(state, va_img, img_data, is_ref, w, h, bpc);

    vaUnmapBuffer(va_dpy, va_img.buf);
    vaDestroyImage(va_dpy, va_img.image_id);
    if (rc)
        return rc;
    if (cerr) {
        vmaf_log(VMAF_LOG_LEVEL_ERROR, "vmaf_sycl readback: chroma import failed (%d)\n", cerr);
        return cerr;
    }
    if (vmaf_sycl_shared_chroma_note_side(state, is_ref))
        vmaf_sycl_shared_chroma_mark_imported(state);
    return 0;
}

sycl::event detile_linear(sycl::queue *q, void *target_buf, const void *imported_ptr,
                          uint32_t y_offset, uint32_t y_pitch, size_t row_bytes, unsigned h,
                          unsigned bpc, unsigned w)
{
    sycl::event ev;
    if (y_pitch == row_bytes && y_offset == 0) {
        ev = q->memcpy(target_buf, imported_ptr, row_bytes * h);
    } else {
        for (unsigned row = 0; row < h; row++) {
            ev =
                q->memcpy(static_cast<uint8_t *>(target_buf) + static_cast<size_t>(row) * row_bytes,
                          static_cast<const uint8_t *>(imported_ptr) + y_offset +
                              static_cast<size_t>(row) * y_pitch,
                          row_bytes);
        }
    }
    if (bpc > 8)
        ev = launch_p010_normalize(q, target_buf, w, h, bpc);
    return ev;
}

sycl::event detile_tile4(sycl::queue *q, void *target_buf, const void *imported_ptr,
                         uint32_t y_offset, uint32_t y_pitch, size_t row_bytes, unsigned h,
                         unsigned bpc)
{
    const auto *src = static_cast<const uint8_t *>(imported_ptr) + y_offset;
    auto *dst = static_cast<uint8_t *>(target_buf);
    unsigned const tiles_per_row = y_pitch / 128;
    unsigned const words_per_tile_row = 128 / 4; /* = 32 */
    unsigned const words_per_row = tiles_per_row * words_per_tile_row;
    /* Fuse P010/P012 MSB→LSB normalization (ADR-1121 follow-up). */
    const bool do_shift = (bpc > 8);
    const unsigned shift = do_shift ? (16u - bpc) : 0u;

    return q->parallel_for(sycl::range<2>(h, words_per_row), [=](sycl::id<2> id) {
        unsigned const py = id[0];
        unsigned const word_x = id[1];

        /* Tile address */
        unsigned const tc = word_x / words_per_tile_row;
        unsigned const wt = word_x % words_per_tile_row;
        unsigned const tr = py / 32;
        unsigned const ity = py % 32;

        /* Tile4 intra-tile swizzle */
        unsigned const x_byte = wt * 4;
        unsigned const swizzled = (x_byte & 0x0F)              /* [3:0]  = x[3:0] */
                                  | ((ity & 3) << 4)           /* [5:4]  = y[1:0] */
                                  | (((x_byte >> 4) & 3) << 6) /* [7:6]  = x[5:4] */
                                  | (((ity >> 2) & 1) << 8)    /* [8]    = y[2]   */
                                  | (((x_byte >> 6) & 1) << 9) /* [9]    = x[6]   */
                                  | (((ity >> 3) & 1) << 10)   /* [10]   = y[3]   */
                                  | (((ity >> 4) & 1) << 11);  /* [11]   = y[4]   */

        size_t const src_off = (size_t)(tr * tiles_per_row + tc) * 4096 + swizzled;

        /* Linear destination */
        size_t const dst_off = (size_t)py * row_bytes + (size_t)tc * 128 + (size_t)wt * 4;
        size_t const row_end = (size_t)(py + 1) * row_bytes;

        /* Bounds check — last tile column may exceed frame width */
        if (dst_off + 4 <= row_end) {
            uint32_t v = *(const uint32_t *)(src + src_off);
            if (do_shift) {
                uint16_t const s0 = (uint16_t)((uint16_t)(v & 0xFFFFu) >> shift);
                uint16_t const s1 = (uint16_t)((uint16_t)(v >> 16) >> shift);
                v = (uint32_t)s0 | ((uint32_t)s1 << 16);
            }
            *(uint32_t *)(dst + dst_off) = v;
        } else if (dst_off < row_end) {
            size_t const remain = row_end - dst_off;
            if (do_shift && remain == 2) {
                uint16_t const raw = (uint16_t)((uint16_t)src[src_off] |
                                                (uint16_t)((uint16_t)src[src_off + 1] << 8));
                uint16_t const s = (uint16_t)(raw >> shift);
                dst[dst_off] = (uint8_t)(s & 0xFFu);
                dst[dst_off + 1] = (uint8_t)(s >> 8);
            } else {
                for (size_t b = 0; b < remain; b++)
                    dst[dst_off + b] = src[src_off + b];
            }
        }
    });
}

sycl::event detile_y_tiled(sycl::queue *q, void *target_buf, const void *imported_ptr,
                           uint32_t y_offset, uint32_t y_pitch, size_t row_bytes, unsigned h,
                           unsigned bpc)
{
    const auto *src = static_cast<const uint8_t *>(imported_ptr) + y_offset;
    auto *dst = static_cast<uint8_t *>(target_buf);
    unsigned const tiles_per_row = y_pitch / 128;
    unsigned const words_per_tile_row = 128 / 4;
    unsigned const words_per_row = tiles_per_row * words_per_tile_row;
    const bool do_shift = (bpc > 8);
    const unsigned shift = do_shift ? (16u - bpc) : 0u;

    return q->parallel_for(sycl::range<2>(h, words_per_row), [=](sycl::id<2> id) {
        unsigned const py = id[0];
        unsigned const word_x = id[1];

        unsigned const tc = word_x / words_per_tile_row;
        unsigned const wt = word_x % words_per_tile_row;
        unsigned const tr = py / 32;
        unsigned const ity = py % 32;

        /* Y-tiled address: OWord column-major */
        unsigned const in_tile_byte_x = wt * 4;
        unsigned const oword_col = in_tile_byte_x / 16;
        unsigned const oword_byte = in_tile_byte_x % 16;

        size_t const src_off = (size_t)(tr * tiles_per_row + tc) * 4096 + (size_t)oword_col * 512 +
                               (size_t)ity * 16 + oword_byte;

        size_t const dst_off = (size_t)py * row_bytes + (size_t)tc * 128 + (size_t)wt * 4;
        size_t const row_end = (size_t)(py + 1) * row_bytes;

        if (dst_off + 4 <= row_end) {
            uint32_t v = *(const uint32_t *)(src + src_off);
            if (do_shift) {
                uint16_t const s0 = (uint16_t)((uint16_t)(v & 0xFFFFu) >> shift);
                uint16_t const s1 = (uint16_t)((uint16_t)(v >> 16) >> shift);
                v = (uint32_t)s0 | ((uint32_t)s1 << 16);
            }
            *(uint32_t *)(dst + dst_off) = v;
        } else if (dst_off < row_end) {
            size_t const remain = row_end - dst_off;
            if (do_shift && remain == 2) {
                uint16_t const raw = (uint16_t)((uint16_t)src[src_off] |
                                                (uint16_t)((uint16_t)src[src_off + 1] << 8));
                uint16_t const s = (uint16_t)(raw >> shift);
                dst[dst_off] = (uint8_t)(s & 0xFFu);
                dst[dst_off + 1] = (uint8_t)(s >> 8);
            } else {
                for (size_t b = 0; b < remain; b++)
                    dst[dst_off + b] = src[src_off + b];
            }
        }
    });
}

void log_zero_copy_once(uint64_t modifier, unsigned w, unsigned h, unsigned bpp, uint32_t y_pitch)
{
    static bool logged_zero_copy = false;
    if (!logged_zero_copy) {
        const char *tiling_name = modifier == DRM_FORMAT_MOD_LINEAR   ? "LINEAR" :
                                  modifier == I915_FORMAT_MOD_4_TILED ? "Tile4" :
                                  modifier == I915_FORMAT_MOD_Y_TILED ? "Y-tiled" :
                                                                        "unknown";
        vmaf_log(VMAF_LOG_LEVEL_INFO,
                 "VA surface zero-copy: DMA-BUF → Level Zero → %s de-tile "
                 "(%ux%u @ %u bpp, pitch=%u)\n",
                 tiling_name, w, h, bpp, y_pitch);
        logged_zero_copy = true;
    }
}

int export_va_drm_prime(VADisplay va_dpy, VASurfaceID va_surf, VADRMPRIMESurfaceDescriptor *desc)
{
    VAStatus const va_st = vaExportSurfaceHandle(
        va_dpy, va_surf, VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
        VA_EXPORT_SURFACE_READ_ONLY | VA_EXPORT_SURFACE_SEPARATE_LAYERS, desc);
    if (va_st != VA_STATUS_SUCCESS) {
        vmaf_log(VMAF_LOG_LEVEL_INFO, "vaExportSurfaceHandle failed: %s — using readback path\n",
                 vaErrorStr(va_st));
        return -EAGAIN;
    }
    if (desc->num_layers < 1 || desc->num_objects < 1 ||
        desc->layers[0].object_index[0] >= desc->num_objects) {
        for (uint32_t i = 0; i < desc->num_objects; i++)
            (void)close(desc->objects[i].fd);
        vmaf_log(VMAF_LOG_LEVEL_ERROR, "DRM PRIME descriptor has no layers\n");
        return -EIO;
    }
    return 0;
}

/* No cross-engine DMA-BUF sync here: real fix is separate QSV sessions (ADR-1121). */
/* Imports the luma object and, when the UV plane lives in another object,
 * that one too (`*chroma_ptr`, else nullptr). On failure nothing stays
 * imported. Every exported fd is closed either way. */
int import_dma_buf_and_close_fds(VmafSyclState *state, const VADRMPRIMESurfaceDescriptor &desc,
                                 const VaChromaPlan &cplan, void **imported_ptr, void **chroma_ptr)
{
    uint32_t const y_obj_idx = desc.layers[0].object_index[0];
    int const y_fd = desc.objects[y_obj_idx].fd;
    uint32_t const y_size = desc.objects[y_obj_idx].size;

    *chroma_ptr = nullptr;
    int err = vmaf_sycl_dmabuf_import(state, y_fd, y_size, imported_ptr);
    if (!err && cplan.obj != y_obj_idx) {
        err =
            vmaf_sycl_dmabuf_import(state, desc.objects[cplan.obj].fd, cplan.obj_size, chroma_ptr);
        if (err) {
            vmaf_sycl_dmabuf_free(state, *imported_ptr);
            *imported_ptr = nullptr;
        }
    }
    for (uint32_t i = 0; i < desc.num_objects; i++)
        (void)close(desc.objects[i].fd);
    return err;
}

/* A de-tile submit that threw: drain what was enqueued before it (those
 * copies still read the import), release the import and report -EIO, as the
 * readback path does. Nothing may throw out of here either. */
int detile_submit_failed(VmafSyclState *state, sycl::queue *q, void *imported_ptr, void *chroma_ptr,
                         const char *what)
{
    vmaf_log(VMAF_LOG_LEVEL_ERROR, "vmaf_sycl de-tile submit failed: %s\n", what);
    try {
        q->wait();
    } catch (...) {
        /* Best-effort drain: the import is released below either way. */
        (void)0;
    }
    vmaf_sycl_dmabuf_free(state, imported_ptr);
    vmaf_sycl_dmabuf_free(state, chroma_ptr);
    return -EIO;
}

int dispatch_detile(VmafSyclState *state, sycl::queue *q, void *target_buf, void *imported_ptr,
                    void *chroma_ptr, uint64_t modifier, uint32_t y_offset, uint32_t y_pitch,
                    size_t row_bytes, unsigned w, unsigned h, unsigned bpc, int is_ref,
                    void *va_display_handle, unsigned int va_surface_id)
{
    const bool linear = modifier == DRM_FORMAT_MOD_LINEAR || modifier == 0;
    if (!linear && modifier != I915_FORMAT_MOD_4_TILED && modifier != I915_FORMAT_MOD_Y_TILED) {
        vmaf_sycl_dmabuf_free(state, imported_ptr);
        vmaf_sycl_dmabuf_free(state, chroma_ptr);
        vmaf_log(VMAF_LOG_LEVEL_WARNING,
                 "Unknown DRM modifier 0x%" PRIx64 " — using readback path\n", modifier);
        return vmaf_sycl_import_va_surface_readback(state, va_display_handle, va_surface_id, is_ref,
                                                    w, h, bpc);
    }

    /* A submit can throw a synchronous sycl::exception (a kernel the device
     * cannot build, an allocation the runtime cannot make). It must not leave
     * this function: the caller is extern "C", and an exception that crosses
     * that boundary ends the process. */
    sycl::event ev;
    try {
        if (linear) {
            vmaf_log(VMAF_LOG_LEVEL_DEBUG, "[%s] zero-copy: LINEAR D2D\n", is_ref ? "ref" : "dis");
            ev =
                detile_linear(q, target_buf, imported_ptr, y_offset, y_pitch, row_bytes, h, bpc, w);
        } else if (modifier == I915_FORMAT_MOD_4_TILED) {
            vmaf_log(VMAF_LOG_LEVEL_DEBUG, "[%s] zero-copy: Tile4 de-tile kernel\n",
                     is_ref ? "ref" : "dis");
            ev = detile_tile4(q, target_buf, imported_ptr, y_offset, y_pitch, row_bytes, h, bpc);
        } else {
            vmaf_log(VMAF_LOG_LEVEL_DEBUG, "[%s] zero-copy: Y-tiled de-tile kernel\n",
                     is_ref ? "ref" : "dis");
            ev = detile_y_tiled(q, target_buf, imported_ptr, y_offset, y_pitch, row_bytes, h, bpc);
        }
    } catch (const sycl::exception &e) {
        return detile_submit_failed(state, q, imported_ptr, chroma_ptr, e.what());
    } catch (const std::exception &e) {
        return detile_submit_failed(state, q, imported_ptr, chroma_ptr, e.what());
    }

    vmaf_sycl_set_detile_event(state, &ev);
    vmaf_sycl_defer_import_free(state, imported_ptr);
    if (chroma_ptr)
        vmaf_sycl_defer_import_free(state, chroma_ptr);
    return 0;
}

/* UV after luma on the same in-order queue (the chroma event then covers the
 * luma one). Both objects stay alive until the next queue wait. */
int launch_va_chroma(VmafSyclState *state, VaChromaPlan *cplan, void *imported_ptr,
                     void *chroma_ptr, int is_ref)
{
    cplan->src.base = chroma_ptr ? chroma_ptr : imported_ptr;
    int const cerr = va_chroma_launch(state, cplan->src, cplan->obj_size, is_ref, false);
    if (cerr) {
        vmaf_log(VMAF_LOG_LEVEL_ERROR, "[%s] chroma import failed (%d)\n", is_ref ? "ref" : "dis",
                 cerr);
        return cerr;
    }
    if (vmaf_sycl_shared_chroma_note_side(state, is_ref))
        vmaf_sycl_shared_chroma_mark_imported(state);
    return 0;
}

/* The arguments of one vmaf_sycl_import_va_surface() call. */
struct VaSurfaceArgs {
    VmafSyclState *state;
    void *va_display_handle;
    unsigned int va_surface_id;
    int is_ref;
    unsigned w;
    unsigned h;
    unsigned bpc;
};

int va_readback(const VaSurfaceArgs &a)
{
    return vmaf_sycl_import_va_surface_readback(a.state, a.va_display_handle, a.va_surface_id,
                                                a.is_ref, a.w, a.h, a.bpc);
}

/* Descriptor dump for every exported layer and object (ADR-1597 chroma-import
 * probe). Same cached VMAF_SYCL_IMPORT_DEBUG switch as the per-import line;
 * logging only. */
void log_va_descriptor(const VaSurfaceArgs &a, const VADRMPRIMESurfaceDescriptor &desc)
{
    if (!vmaf_sycl_import_debug_enabled(a.state))
        return;
    vmaf_log(VMAF_LOG_LEVEL_INFO, "VMAF_SYCL_IMPORT_DEBUG [%s] num_layers=%u num_objects=%u\n",
             a.is_ref ? "ref" : "dis", desc.num_layers, desc.num_objects);
    for (uint32_t i = 0; i < desc.num_layers && i < 4; i++) {
        vmaf_log(VMAF_LOG_LEVEL_INFO,
                 "VMAF_SYCL_IMPORT_DEBUG layer[%u] drm_format=0x%08x num_planes=%u obj=%u "
                 "offset=%u pitch=%u\n",
                 i, desc.layers[i].drm_format, desc.layers[i].num_planes,
                 desc.layers[i].object_index[0], desc.layers[i].offset[0], desc.layers[i].pitch[0]);
    }
    for (uint32_t i = 0; i < desc.num_objects && i < 4; i++) {
        vmaf_log(VMAF_LOG_LEVEL_INFO,
                 "VMAF_SYCL_IMPORT_DEBUG obj[%u] size=%u modifier=0x%016" PRIx64 "\n", i,
                 desc.objects[i].size, static_cast<uint64_t>(desc.objects[i].drm_format_modifier));
    }
}

/* Chroma (layers[1]) plan, resolved before any import so every failure is one
 * close-all-fds exit (T-12-11, T-12-12). -ENOTSUP: take the readback path. */
int plan_va_chroma(const VaSurfaceArgs &a, const VADRMPRIMESurfaceDescriptor &desc,
                   VaChromaPlan *cplan)
{
    int const cplan_err = va_chroma_plan(desc, a.w, a.h, a.bpc, cplan);
    if (!cplan_err)
        return 0;
    for (uint32_t i = 0; i < desc.num_objects; i++)
        (void)close(desc.objects[i].fd);
    if (cplan_err == -ENOTSUP) {
        vmaf_log(VMAF_LOG_LEVEL_WARNING,
                 "No chroma layout for the UV object's DRM modifier — using readback path\n");
    } else {
        vmaf_log(VMAF_LOG_LEVEL_ERROR, "DRM PRIME descriptor has no usable UV layer (%d)\n",
                 cplan_err);
    }
    return cplan_err;
}

/* Imports the exported objects, de-tiles luma into the upload slot and
 * de-interleaves UV behind it. Every exported fd is closed here. */
int import_exported_surface(const VaSurfaceArgs &a, const VADRMPRIMESurfaceDescriptor &desc,
                            VaChromaPlan *cplan)
{
    uint64_t const modifier = desc.objects[desc.layers[0].object_index[0]].drm_format_modifier;
    uint32_t const y_offset = desc.layers[0].offset[0];
    uint32_t const y_pitch = desc.layers[0].pitch[0];
    unsigned const bpp = (a.bpc + 7) / 8;

    void *imported_ptr = nullptr;
    void *chroma_ptr = nullptr; /* only when the UV plane is in another object */
    int const err = import_dma_buf_and_close_fds(a.state, desc, *cplan, &imported_ptr, &chroma_ptr);
    if (err) {
        vmaf_log(VMAF_LOG_LEVEL_INFO, "DMA-BUF import failed (%d) — using readback path\n", err);
        return va_readback(a);
    }

    void *target_buf = a.is_ref ? vmaf_sycl_get_shared_ref_upload(a.state) :
                                  vmaf_sycl_get_shared_dis_upload(a.state);
    if (!target_buf) {
        vmaf_sycl_dmabuf_free(a.state, imported_ptr);
        vmaf_sycl_dmabuf_free(a.state, chroma_ptr);
        vmaf_log(VMAF_LOG_LEVEL_ERROR, "Shared frame buffer not initialised\n");
        return -EINVAL;
    }

    if (vmaf_sycl_import_debug_enabled(a.state)) {
        vmaf_log(VMAF_LOG_LEVEL_INFO,
                 "VMAF_SYCL_IMPORT_DEBUG [%s] va_surf=%u imported_ptr=%p target_buf=%p\n",
                 a.is_ref ? "ref" : "dis", a.va_surface_id, imported_ptr, target_buf);
    }

    auto *q = static_cast<sycl::queue *>(vmaf_sycl_get_queue_ptr(a.state));
    size_t const row_bytes = static_cast<size_t>(a.w) * bpp;
    log_zero_copy_once(modifier, a.w, a.h, bpp, y_pitch);

    const bool linear = modifier == DRM_FORMAT_MOD_LINEAR || modifier == 0;
    const bool known =
        linear || modifier == I915_FORMAT_MOD_4_TILED || modifier == I915_FORMAT_MOD_Y_TILED;
    int const derr = dispatch_detile(a.state, q, target_buf, imported_ptr, chroma_ptr, modifier,
                                     y_offset, y_pitch, row_bytes, a.w, a.h, a.bpc, a.is_ref,
                                     a.va_display_handle, a.va_surface_id);
    /* An unknown modifier took the readback path, which imports chroma itself. */
    if (derr || !known)
        return derr;
    return launch_va_chroma(a.state, cplan, imported_ptr, chroma_ptr, a.is_ref);
}

} // namespace

namespace
{

/* The import proper; vmaf_sycl_import_va_surface() times every return path of it. */
int import_va_surface_body(VmafSyclState *state, void *va_display_handle,
                           unsigned int va_surface_id, int is_ref, unsigned w, unsigned h,
                           unsigned bpc)
{
    const VaSurfaceArgs args = {.state = state,
                                .va_display_handle = va_display_handle,
                                .va_surface_id = va_surface_id,
                                .is_ref = is_ref,
                                .w = w,
                                .h = h,
                                .bpc = bpc};
    auto va_dpy = static_cast<VADisplay>(va_display_handle);
    auto const va_surf = static_cast<VASurfaceID>(va_surface_id);

    /* Sync the VA surface to ensure decode is complete */
    VAStatus const va_st = vaSyncSurface(va_dpy, va_surf);
    if (va_st != VA_STATUS_SUCCESS)
        vmaf_log(VMAF_LOG_LEVEL_WARNING, "vaSyncSurface failed: %s\n", vaErrorStr(va_st));

    /* Export the VA surface as DRM PRIME2 (DMA-BUF fd + layout info) */
    VADRMPRIMESurfaceDescriptor desc = {};
    int const exp_rc = export_va_drm_prime(va_dpy, va_surf, &desc);
    if (exp_rc == -EAGAIN)
        return va_readback(args);
    if (exp_rc != 0)
        return exp_rc;
    log_va_descriptor(args, desc);

    VaChromaPlan cplan = {};
    int const cplan_err = plan_va_chroma(args, desc, &cplan);
    if (cplan_err == -ENOTSUP)
        return va_readback(args);
    if (cplan_err)
        return cplan_err;
    return import_exported_surface(args, desc, &cplan);
}

} // namespace

extern "C" int vmaf_sycl_import_va_surface(VmafSyclState *state, void *va_display_handle,
                                           unsigned int va_surface_id, int is_ref, unsigned w,
                                           unsigned h, unsigned bpc)
{
    if (!state || !va_display_handle)
        return -EINVAL;
    double const t_import = vmaf_sycl_phase_start(state);
    int const rc =
        import_va_surface_body(state, va_display_handle, va_surface_id, is_ref, w, h, bpc);
    vmaf_sycl_phase_record(state, VMAF_SYCL_PHASE_IMPORT, t_import);
    return rc;
}

/* Internal test entry (core/test/test_sycl_detile_tile4.c): run the Tile4
 * de-tile of one plane at offset 0 and wait for it. */
extern "C" int vmaf_sycl_detile_tile4_for_test(VmafSyclState *state, void *dst,
                                               const void *src_tiled, size_t pitch,
                                               size_t row_bytes, unsigned h, unsigned bpc)
{
    if (!state || !dst || !src_tiled || h == 0 || row_bytes == 0 || pitch % 128 != 0 ||
        pitch > UINT32_MAX || row_bytes > pitch || bpc < 8 || bpc > 16)
        return -EINVAL;
    auto *q = static_cast<sycl::queue *>(vmaf_sycl_get_queue_ptr(state));
    if (!q)
        return -EINVAL;
    try {
        detile_tile4(q, dst, src_tiled, 0, (uint32_t)pitch, row_bytes, h, bpc).wait_and_throw();
    } catch (const sycl::exception &e) {
        vmaf_log(VMAF_LOG_LEVEL_ERROR, "vmaf_sycl de-tile test run failed: %s\n", e.what());
        return -EIO;
    } catch (const std::exception &e) {
        vmaf_log(VMAF_LOG_LEVEL_ERROR, "vmaf_sycl de-tile test run failed: %s\n", e.what());
        return -EIO;
    }
    return 0;
}

#else /* !HAVE_SYCL_DMABUF */

extern "C" int vmaf_sycl_import_va_surface(VmafSyclState *state, void *va_display_handle,
                                           unsigned int va_surface_id, int is_ref, unsigned w,
                                           unsigned h, unsigned bpc)
{
    (void)state;
    (void)va_display_handle;
    (void)va_surface_id;
    (void)is_ref;
    (void)w;
    (void)h;
    (void)bpc;
    return -ENOTSUP;
}

extern "C" int vmaf_sycl_detile_tile4_for_test(VmafSyclState *state, void *dst,
                                               const void *src_tiled, size_t pitch,
                                               size_t row_bytes, unsigned h, unsigned bpc)
{
    (void)state;
    (void)dst;
    (void)src_tiled;
    (void)pitch;
    (void)row_bytes;
    (void)h;
    (void)bpc;
    return -ENOSYS;
}

#endif /* HAVE_SYCL_DMABUF */

#else /* _WIN32 */

/* ------------------------------------------------------------------ */
/* Windows stubs — DMA-BUF is a Linux-only kernel interface.          */
/* Level Zero on Windows uses NT handles, not DMA-BUF fds; this TU's  */
/* fd-based import path cannot and does not compile on Windows.        */
/* Callers should prefer d3d11_import.cpp on Windows.                  */
/* ------------------------------------------------------------------ */

#include <errno.h>

extern "C" {
#include "dmabuf_import.h"
#include "log.h"
}

extern "C" int vmaf_sycl_dmabuf_import(VmafSyclState *state, int fd, size_t size, void **ptr)
{
    (void)state;
    (void)fd;
    (void)size;
    (void)ptr;
    vmaf_log(VMAF_LOG_LEVEL_ERROR,
             "libvmaf: SYCL DMA-BUF import is not supported on Windows "
             "(DMA-BUF is a Linux kernel primitive; Windows zero-copy requires DXGI NT handles)\n");
    return -ENOSYS;
}

extern "C" void vmaf_sycl_dmabuf_free(VmafSyclState *state, void *ptr)
{
    (void)state;
    (void)ptr;
}

extern "C" int vmaf_sycl_import_va_surface(VmafSyclState *state, void *va_display_handle,
                                           unsigned int va_surface_id, int is_ref, unsigned w,
                                           unsigned h, unsigned bpc)
{
    (void)state;
    (void)va_display_handle;
    (void)va_surface_id;
    (void)is_ref;
    (void)w;
    (void)h;
    (void)bpc;
    vmaf_log(VMAF_LOG_LEVEL_ERROR,
             "libvmaf: SYCL VA surface import is not supported on Windows "
             "(VA-API is a Linux primitive; Windows zero-copy requires DXGI NT handles)\n");
    return -ENOSYS;
}

extern "C" int vmaf_sycl_detile_tile4_for_test(VmafSyclState *state, void *dst,
                                               const void *src_tiled, size_t pitch,
                                               size_t row_bytes, unsigned h, unsigned bpc)
{
    (void)state;
    (void)dst;
    (void)src_tiled;
    (void)pitch;
    (void)row_bytes;
    (void)h;
    (void)bpc;
    return -ENOSYS;
}

#endif /* _WIN32 */

#endif /* HAVE_SYCL */
