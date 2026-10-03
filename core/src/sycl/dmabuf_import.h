/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

#ifndef VMAF_SRC_SYCL_DMABUF_IMPORT_H_
#define VMAF_SRC_SYCL_DMABUF_IMPORT_H_

#include "config.h"

#if HAVE_SYCL

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NOLINTNEXTLINE(modernize-use-using): one definition for C and C++; `using` is not C. ADR-0141. */
typedef struct VmafSyclState VmafSyclState;

/**
 * Import a DMA-BUF file descriptor as a SYCL device pointer via
 * Level Zero external memory import.
 *
 * The caller retains ownership of the fd and must close it when done.
 * The returned pointer must be freed with vmaf_sycl_dmabuf_free().
 *
 * @param state   The SYCL state (must use Level Zero backend).
 * @param fd      DMA-BUF file descriptor.
 * @param size    Size of the DMA-BUF allocation in bytes.
 * @param[out] ptr  Receives the SYCL-accessible device pointer.
 *
 * @return 0 on success, negative errno on failure.
 */
int vmaf_sycl_dmabuf_import(VmafSyclState *state, int fd, size_t size, void **ptr);

/**
 * Free a pointer returned by vmaf_sycl_dmabuf_import().
 *
 * @param state  The SYCL state.
 * @param ptr    Pointer to free (previously returned by dmabuf_import).
 */
void vmaf_sycl_dmabuf_free(VmafSyclState *state, void *ptr);

/**
 * Import a VA surface Y-plane into the SYCL shared frame buffers.
 *
 * Primary path (zero-copy): exports the VA surface as a DRM PRIME2
 * DMA-BUF handle, imports it via Level Zero, and uses a SYCL compute
 * kernel to de-tile (Tile4/Y-tiled → linear) directly into the shared
 * buffer. Everything stays on GPU — no CPU copies, no PCIe pixel traffic.
 *
 * Fallback path: if the export or DMA-BUF import fails, uses
 * vaGetImage + vaMapBuffer + H2D memcpy (GPU→CPU→GPU).
 *
 * The VA surface must be in NV12 or P010 format. Luma and the 4:2:0 UV
 * plane (`layers[1]`) are imported: luma into the shared luma buffer, UV
 * de-interleaved into the shared planar Cb / Cr planes of the upload slot
 * (ADR-1597). A frame's chroma becomes current once both the ref and the dis
 * surface were imported (vmaf_sycl_shared_chroma_mark_imported()). A
 * descriptor without a usable second layer fails with -EINVAL.
 *
 * @param state       The SYCL state (shared frame buffers must be initialised).
 * @param va_display  The VA display handle (VADisplay).
 * @param va_surface  The VA surface ID (VASurfaceID = unsigned int).
 * @param is_ref      If true, copy to shared_ref_buf; else shared_dis_buf.
 * @param w           Frame width in pixels.
 * @param h           Frame height in pixels.
 * @param bpc         Bits per component (8 or 10).
 *
 * @return 0 on success, negative errno on failure.
 */
int vmaf_sycl_import_va_surface(VmafSyclState *state, void *va_display, unsigned int va_surface,
                                int is_ref, unsigned w, unsigned h, unsigned bpc);

#ifdef __cplusplus
}
#endif

#endif /* HAVE_SYCL */

#endif /* VMAF_SRC_SYCL_DMABUF_IMPORT_H_ */
