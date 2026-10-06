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

#ifndef VMAF_SRC_SYCL_COMMON_H_
#define VMAF_SRC_SYCL_COMMON_H_

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "config.h"
#include "picture.h"

#if HAVE_SYCL

#include <libvmaf/libvmaf_sycl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * VmafSyclState internals — opaque to C callers, defined in common.cpp.
 *
 * The state owns:
 *   - A sycl::queue (in-order, optionally with profiling)
 *   - Shared frame buffers (USM device allocations for ref/dis Y planes)
 *   - Profiling accumulators
 */

/**
 * Count the SYCL kernels whose device images this program registered.
 *
 * Needs no device: it asks the SYCL runtime for the kernel IDs every linked
 * device image declared. Zero means the images were never registered, which
 * is how a link that skipped the device-image wrapper fails (ADR-1364); the
 * first kernel submit would then fail with "No kernel named ... was found".
 *
 * @return Number of registered kernels, or -EIO if the runtime threw.
 */
int vmaf_sycl_registered_kernel_count(void);

/**
 * Guard for a SYCL extractor submit that reads Cb / Cr (ADR-1597).
 *
 * With host pictures the chroma is uploaded by vmaf_sycl_shared_chroma_upload()
 * and this returns 0. Without them (zero-copy input) the chroma is valid only
 * when the import marked it (vmaf_sycl_shared_chroma_mark_imported()) and
 * vmaf_sycl_advance_frame() promoted it for this frame; otherwise the shared
 * planes hold an older frame and the extractor must not read them.
 *
 * @param extractor  Registered extractor name, used in the log line.
 * @param ref        Reference picture the submit would read, or NULL.
 * @param dis        Distorted picture the submit would read, or NULL.
 *
 * @return 0 when both pictures are non-NULL or the chroma is current for this
 *         frame; otherwise logs one error naming the extractor and returns
 *         -ENOTSUP.
 */
int vmaf_sycl_require_chroma(const VmafSyclState *state, const char *extractor,
                             const VmafPicture *ref, const VmafPicture *dis);

/* ---- Device-memory helpers (USM wrappers) ---- */

/**
 * Allocate USM device memory on the SYCL device.
 *
 * @param state  The SYCL state.
 * @param size   Number of bytes to allocate.
 *
 * @return Non-NULL pointer to device memory, or NULL on failure.
 */
void *vmaf_sycl_malloc_device(VmafSyclState *state, size_t size);

/**
 * Allocate USM host memory (host-accessible, device-accessible on iGPU).
 *
 * @param state  The SYCL state.
 * @param size   Number of bytes to allocate.
 *
 * @return Non-NULL pointer to host memory, or NULL on failure.
 */
void *vmaf_sycl_malloc_host(VmafSyclState *state, size_t size);

/**
 * Free USM memory (device or host).
 *
 * @param state  The SYCL state.
 * @param ptr    Pointer returned by vmaf_sycl_malloc_device/host.
 */
void vmaf_sycl_free(VmafSyclState *state, void *ptr);

/**
 * Synchronous host→device copy.
 *
 * @param state  The SYCL state.
 * @param dst    Device pointer (destination).
 * @param src    Host pointer (source).
 * @param size   Number of bytes to copy.
 *
 * @return 0 on success, negative errno on failure.
 */
int vmaf_sycl_memcpy_h2d(VmafSyclState *state, void *dst, const void *src, size_t size);

/**
 * Synchronous device→host copy.
 *
 * @param state  The SYCL state.
 * @param dst    Host pointer (destination).
 * @param src    Device pointer (source).
 * @param size   Number of bytes to copy.
 *
 * @return 0 on success, negative errno on failure.
 */
int vmaf_sycl_memcpy_d2h(VmafSyclState *state, void *dst, const void *src, size_t size);

/**
 * Asynchronous host→device copy (returns immediately, work queued).
 *
 * @param state  The SYCL state.
 * @param dst    Device pointer (destination).
 * @param src    Host pointer (source).
 * @param size   Number of bytes to copy.
 *
 * @return 0 on success, negative errno on failure.
 */
int vmaf_sycl_memcpy_h2d_async(VmafSyclState *state, void *dst, const void *src, size_t size);

/* ---- Queue synchronization ---- */

/**
 * Wait for all enqueued SYCL work to complete (primary queue).
 *
 * @param state  The SYCL state.
 *
 * @return 0 on success, negative errno on failure.
 */
int vmaf_sycl_queue_wait(VmafSyclState *state);

/**
 * Wait for all pending copy/upload operations on the copy queue.
 * Must be called after vmaf_sycl_shared_frame_upload() and before
 * extractor compute queues start work.
 *
 * @param state  The SYCL state.
 *
 * @return 0 on success, negative errno on failure.
 */
int vmaf_sycl_wait_copy_queue(VmafSyclState *state);

/**
 * Wait for just the last shared-frame upload event to complete.
 * Lighter than wait_copy_queue — only waits on the DMA event,
 * not the entire copy queue.
 *
 * @param state  The SYCL state.
 *
 * @return 0 on success, negative errno on failure.
 */
int vmaf_sycl_wait_last_upload(VmafSyclState *state);

/* ---- Per-extractor compute queue management ---- */

/**
 * Create a new in-order compute queue on the same device and context.
 * Each extractor should create its own queue to enable GPU-level
 * parallelism between extractors. Respects the profiling flag from
 * the original SYCL state.
 *
 * @param state  The SYCL state (provides device, context, profiling flag).
 *
 * @return Opaque pointer to sycl::queue (cast in C++ code), or NULL.
 *         Caller owns the queue and must free with vmaf_sycl_destroy_queue().
 */
void *vmaf_sycl_create_compute_queue(VmafSyclState *state);

/**
 * Destroy a queue created by vmaf_sycl_create_compute_queue().
 * Waits for any pending work before destruction.
 *
 * @param queue_ptr  Opaque pointer returned by vmaf_sycl_create_compute_queue().
 */
void vmaf_sycl_destroy_queue(void *queue_ptr);

/* ---- Shared frame buffer management ---- */

/**
 * Allocate shared ref+dis Y-plane device buffers.
 *
 * @param state  The SYCL state.
 * @param w      Frame width in pixels.
 * @param h      Frame height in pixels.
 * @param bpc    Bits per component (8 or 10).
 *
 * @return 0 on success, negative errno on failure.
 */
int vmaf_sycl_shared_frame_init(VmafSyclState *state, unsigned w, unsigned h, unsigned bpc);

/**
 * Get pointers to the shared ref+dis device buffers.
 *
 * @param state    The SYCL state.
 * @param[out] ref Receives pointer to ref Y-plane device buffer.
 * @param[out] dis Receives pointer to dis Y-plane device buffer.
 *
 * @return 0 on success, negative errno on failure.
 */
int vmaf_sycl_shared_frame_get(VmafSyclState *state, void **ref, void **dis);

/**
 * Upload host Y-plane data to the shared device buffers.
 * Used by the vmaf_read_pictures() path (non-zero-copy).
 *
 * @param state  The SYCL state.
 * @param ref    Reference picture (host buffer).
 * @param dis    Distorted picture (host buffer).
 *
 * @return 0 on success, negative errno on failure.
 */
int vmaf_sycl_shared_frame_upload(VmafSyclState *state, VmafPicture *ref, VmafPicture *dis);

/**
 * Upload a single Y-plane from a host buffer into a shared device buffer.
 * This is the platform-agnostic surface import path: the caller provides
 * a pointer to linear Y-plane pixels (e.g. from VPL MapFrame)
 * and this function performs an H2D copy into the appropriate shared buffer.
 *
 * @param state   The SYCL state (shared frame buffers must be initialised).
 * @param src     Host pointer to Y-plane pixel data (linear layout).
 * @param pitch   Row stride in bytes (may exceed w * bytes_per_pixel).
 * @param is_ref  If non-zero, upload to ref buffer; else dis buffer.
 * @param w       Frame width in pixels.
 * @param h       Frame height in pixels.
 * @param bpc     Bits per component (8 or 10).
 *
 * Synchronous: returns after the copy has completed, so `src` may be freed,
 * unmapped or refilled as soon as the call returns.
 *
 * @return 0 on success, negative errno on failure.
 */
int vmaf_sycl_upload_plane(VmafSyclState *state, const void *src, unsigned pitch, int is_ref,
                           unsigned w, unsigned h, unsigned bpc);

/**
 * Free the shared frame buffers.
 *
 * @param state  The SYCL state.
 */
void vmaf_sycl_shared_frame_close(VmafSyclState *state);

/* ---- Shared chroma planes (opt-in, ADR-1369) ---- */

/**
 * Allocate the shared Cb / Cr planes of ref and dis, double-buffered with the
 * luma slots. Only extractors that read chroma call this, so a luma-only run
 * never uploads chroma. Idempotent for the same geometry.
 *
 * Requires vmaf_sycl_shared_frame_init() first: the sample size comes from its
 * bpc, and the planes are packed at `cw * bytes_per_sample`.
 *
 * @param state  The SYCL state.
 * @param cw     Chroma plane width in samples.
 * @param ch     Chroma plane height in samples.
 *
 * @return 0 on success, -EINVAL without luma planes or on a geometry mismatch,
 *         -ENOMEM on allocation failure.
 */
int vmaf_sycl_shared_chroma_init(VmafSyclState *state, unsigned cw, unsigned ch);

/**
 * Upload the current frame's Cb / Cr planes of ref and dis into the compute
 * slot, once per frame. The first caller of a frame enqueues the copies on the
 * copy queue and makes them part of the last upload event; later callers in
 * the same frame return 0 without copying. Call it after the frame's luma
 * upload (the host read path uploads luma before any extractor submits).
 *
 * @param state  The SYCL state (chroma planes must be initialised).
 * @param ref    Reference picture (host buffer).
 * @param dis    Distorted picture (host buffer).
 *
 * @return 0 on success, -EINVAL on a missing picture, uninitialised planes or
 *         a picture whose chroma geometry differs, -EIO on a SYCL error.
 */
int vmaf_sycl_shared_chroma_upload(VmafSyclState *state, VmafPicture *ref, VmafPicture *dis);

/**
 * Device pointer to one shared plane of the compute slot.
 *
 * @param state   The SYCL state.
 * @param is_ref  Non-zero for the reference picture, zero for the distorted.
 * @param plane   0 = luma, 1 = Cb, 2 = Cr.
 *
 * @return The plane, or NULL when it is not allocated.
 */
void *vmaf_sycl_get_shared_plane(VmafSyclState *state, int is_ref, unsigned plane);

/**
 * Device pointer to one shared plane of the upload slot (the slot the next
 * vmaf_sycl_advance_frame() promotes to the compute slot). The zero-copy VA
 * import writes luma and chroma here (ADR-1597).
 *
 * @param state   The SYCL state.
 * @param is_ref  Non-zero for the reference picture, zero for the distorted.
 * @param plane   0 = luma, 1 = Cb, 2 = Cr.
 *
 * @return The plane, or NULL when it is not allocated.
 */
void *vmaf_sycl_get_shared_plane_upload(VmafSyclState *state, int is_ref, unsigned plane);

/**
 * Record that the upload slot's Cb / Cr planes of ref and dis were written for
 * the frame about to be advanced. vmaf_sycl_advance_frame() turns the mark
 * into "chroma is current for the new frame"; nothing else does (ADR-1597).
 *
 * @param state  The SYCL state.
 */
void vmaf_sycl_shared_chroma_mark_imported(VmafSyclState *state);

/**
 * Note that one side (ref or dis) of the upload slot's Cb / Cr planes was
 * written for the frame about to be advanced. The VA import runs once per
 * side, so a frame's chroma is complete only when both were noted; the notes
 * are cleared by vmaf_sycl_advance_frame() (ADR-1597).
 *
 * @param state   The SYCL state.
 * @param is_ref  Non-zero for the reference side, zero for the distorted.
 *
 * @return true when both sides are now noted for this frame: the caller then
 *         calls vmaf_sycl_shared_chroma_mark_imported().
 */
bool vmaf_sycl_shared_chroma_note_side(VmafSyclState *state, int is_ref);

/**
 * Whether the compute slot's Cb / Cr planes hold the current frame's chroma:
 * they are allocated and an upload or import of this frame produced them.
 *
 * @param state  The SYCL state.
 *
 * @return true when chroma readers may use vmaf_sycl_get_shared_plane().
 */
bool vmaf_sycl_shared_chroma_current(const VmafSyclState *state);

/**
 * Make `queue_ptr` wait on the device for the last shared upload (luma and
 * any chroma) without blocking the host. Extractors that read the shared
 * planes from their own queue call it before their first kernel of a frame;
 * graph-registered extractors get the same barrier from
 * vmaf_sycl_graph_submit().
 *
 * @param state      The SYCL state.
 * @param queue_ptr  Opaque pointer to the sycl::queue that reads the planes.
 *
 * @return 0 on success, -EINVAL on NULL arguments, -EIO on a SYCL error.
 */
int vmaf_sycl_queue_after_upload(VmafSyclState *state, void *queue_ptr);

/* ---- Opaque queue handle for extractor kernels ---- */

/**
 * Get the underlying sycl::queue pointer for use in DPC++ kernel code.
 * The returned pointer is only valid while the state is alive.
 * Cast to sycl::queue* in C++ code.
 *
 * @param state  The SYCL state.
 *
 * @return Opaque pointer to sycl::queue (cast in C++ code), or NULL.
 */
void *vmaf_sycl_get_queue_ptr(VmafSyclState *state);

/**
 * Check if the SYCL device supports double precision (fp64).
 *
 * @param state  The SYCL state.
 *
 * @return true if fp64 is supported, false otherwise.
 */
bool vmaf_sycl_has_fp64(const VmafSyclState *state);

/**
 * Get the shared ref device buffer pointer.
 *
 * @param state  The SYCL state.
 *
 * @return Pointer to ref Y-plane device buffer, or NULL if not initialised.
 */
void *vmaf_sycl_get_shared_ref(VmafSyclState *state);

/**
 * Get the shared dis device buffer pointer.
 *
 * @param state  The SYCL state.
 *
 * @return Pointer to dis Y-plane device buffer, or NULL if not initialised.
 */
void *vmaf_sycl_get_shared_dis(VmafSyclState *state);

/**
 * Get the shared ref device buffer pointer for a specific double-buffer slot.
 *
 * @param state  The SYCL state.
 * @param slot   Buffer slot index (0 or 1).
 *
 * @return Pointer to ref Y-plane device buffer, or NULL.
 */
void *vmaf_sycl_get_shared_ref_slot(VmafSyclState *state, int slot);

/**
 * Get the shared dis device buffer pointer for a specific double-buffer slot.
 *
 * @param state  The SYCL state.
 * @param slot   Buffer slot index (0 or 1).
 *
 * @return Pointer to dis Y-plane device buffer, or NULL.
 */
void *vmaf_sycl_get_shared_dis_slot(VmafSyclState *state, int slot);

/**
 * Get the current compute slot index (0 or 1).
 *
 * @param state  The SYCL state.
 *
 * @return Current compute slot index.
 */
int vmaf_sycl_get_compute_slot(const VmafSyclState *state);

/**
 * Get the shared ref device buffer pointer for the current upload slot.
 * Use this from the VA-import path to target the upload slot (cur_upload),
 * not the live compute slot (cur_compute). After vmaf_sycl_advance_frame()
 * the upload slot becomes the compute slot, so compute reads the
 * freshly-imported frame.
 *
 * @param state  The SYCL state.
 *
 * @return Pointer to ref Y-plane device buffer for cur_upload slot, or NULL.
 */
void *vmaf_sycl_get_shared_ref_upload(VmafSyclState *state);

/**
 * Whether VMAF_SYCL_IMPORT_DEBUG diagnostic logging is enabled.
 *
 * Resolved once in vmaf_sycl_state_init; this accessor lets translation units
 * that hold an opaque VmafSyclState (e.g. dmabuf_import.cpp) read the cached
 * flag without re-calling getenv() per frame.
 *
 * @param state  The SYCL state.
 *
 * @return true when VMAF_SYCL_IMPORT_DEBUG=1 was set at init, false otherwise.
 */
bool vmaf_sycl_import_debug_enabled(const VmafSyclState *state);

/**
 * Get the shared dis device buffer pointer for the current upload slot.
 * Symmetric to vmaf_sycl_get_shared_ref_upload().
 *
 * @param state  The SYCL state.
 *
 * @return Pointer to dis Y-plane device buffer for cur_upload slot, or NULL.
 */
void *vmaf_sycl_get_shared_dis_upload(VmafSyclState *state);

/**
 * Get a pointer to the sycl::event from the last shared-frame upload.
 * Extractors can depend on this event to avoid a CPU-side wait for DMA.
 *
 * @param state  The SYCL state.
 *
 * @return Opaque pointer to sycl::event, or NULL if not initialised.
 */
void *vmaf_sycl_get_last_upload_event(VmafSyclState *state);

/* ---- Diagnostic checksum probe ---- */

/**
 * D2H checksum probe — gated by VMAF_SYCL_CHECKSUM=1 env var.
 *
 * Performs a blocking device-to-host copy of the Y plane from the current
 * compute slot (state->cur_compute) and computes an FNV-32a hash over all
 * bytes.  The result is logged via vmaf_log at INFO level as a single line:
 *   VMAF_SYCL_CHECKSUM path=<path_tag> frame=<frame_index> slot=<N> ref|dis crc=0x<HEX>
 *
 * Zero cost when VMAF_SYCL_CHECKSUM is unset — returns 0 immediately
 * before any allocation or queue work. The variable is resolved once in
 * vmaf_sycl_state_init (like VMAF_SYCL_IMPORT_DEBUG), not per call.
 *
 * Call from both the VA-import path (path_tag="sycl") and the host-upload
 * oracle path (path_tag="host") to capture exactly what compute will read,
 * allowing a per-frame diff to select the Phase 2 fix direction.
 *
 * @param state        SYCL state.
 * @param is_ref       1 = probe ref buffer, 0 = probe dis buffer.
 * @param frame_index  Frame number for the log line (0-based).
 * @param path_tag     Short label printed in the log ("host" or "sycl").
 *
 * @return 0 on success, negative errno on failure (e.g. -ENOMEM, -EIO).
 */
int vmaf_sycl_checksum_y_slot(VmafSyclState *state, int is_ref, unsigned frame_index,
                              const char *path_tag);

/* ---- Combined command graph (merges all extractors into one replay) ---- */

/**
 * Callback types for combined command graph.
 *
 * Work for each extractor is split into three phases:
 *   pre_fn   → memset / clear operations (direct-enqueued OUTSIDE the graph)
 *   enqueue_fn → compute kernel launches (recorded INSIDE the command graph)
 *   post_fn  → D2H memcpy operations   (direct-enqueued OUTSIDE the graph)
 *
 * The Level Zero graph runtime does not reliably replay memcpy/memset
 * nodes, so only pure kernel launches go inside the recorded graph.
 *
 * enqueue_fn: Enqueue only compute kernels for this extractor.
 *   @param queue_ptr    Opaque pointer to sycl::queue.
 *   @param priv         Extractor private state.
 *   @param shared_ref   Device pointer to ref Y-plane.
 *   @param shared_dis   Device pointer to dis Y-plane.
 *
 * pre_fn / post_fn: Device-memory housekeeping (memset / memcpy).
 *   @param queue_ptr    Opaque pointer to sycl::queue.
 *   @param priv         Extractor private state.
 *   May be NULL if not needed.
 *
 * config_fn: Configure extractor state for a specific double-buffer slot.
 *   Called before enqueue_fn during graph recording so that the correct
 *   device pointers are captured (e.g. motion ping-pong buffers).
 *   @param priv  Extractor private state.
 *   @param slot  Double-buffer slot index (0 or 1).
 *   May be NULL if no per-slot configuration is needed.
 */
/* NOLINTBEGIN(modernize-use-using): one definition for C and C++. A `using`
 * copy for C++ next to a `typedef` for C is two signatures to keep equal by
 * hand, and `using` is not C. ADR-0141. */
typedef void (*VmafSyclGraphEnqueueFn)(void *queue_ptr, void *priv, void *shared_ref,
                                       void *shared_dis);
typedef void (*VmafSyclGraphPreFn)(void *queue_ptr, void *priv);
typedef void (*VmafSyclGraphPostFn)(void *queue_ptr, void *priv);
typedef void (*VmafSyclGraphConfigFn)(void *priv, int slot);
/* NOLINTEND(modernize-use-using) */

/**
 * Register an extractor's GPU work with the combined command graph.
 * Must be called during init_fex_sycl for each SYCL extractor.
 *
 * @param state       The SYCL state.
 * @param enqueue_fn  Kernel launches (recorded inside the command graph).
 * @param pre_fn      Pre-graph work: memset ops (may be NULL).
 * @param post_fn     Post-graph work: D2H memcpy ops (may be NULL).
 * @param config_fn   Per-slot configuration for graph recording (may be NULL).
 * @param priv        Extractor private state (passed to callbacks).
 *
 * @return 0 on success, negative errno on failure.
 */
int vmaf_sycl_graph_register(VmafSyclState *state, VmafSyclGraphEnqueueFn enqueue_fn,
                             VmafSyclGraphPreFn pre_fn, VmafSyclGraphPostFn post_fn,
                             VmafSyclGraphConfigFn config_fn, void *priv, const char *name);

/**
 * Unregister an extractor from the combined command graph.
 * Must be called from close_fex_sycl before the extractor's priv is freed.
 * Invalidates any recorded graphs so they are re-recorded on the next
 * vmaf_sycl_graph_submit() call with the remaining extractors.
 *
 * @param state  The SYCL state.
 * @param priv   The extractor private-state pointer used at registration time.
 *
 * @return 0 on success, -EINVAL if state is NULL or priv not found.
 */
int vmaf_sycl_graph_unregister(VmafSyclState *state, const void *priv);

/**
 * Get the combined compute queue for direct submission.
 * All registered extractors share this single queue.
 *
 * @param state  The SYCL state.
 *
 * @return Opaque pointer to sycl::queue, or NULL.
 */
void *vmaf_sycl_get_combined_queue(const VmafSyclState *state);

/**
 * Submit all registered extractors' GPU work for the current frame.
 * Idempotent per frame: enqueues work only when the last extractor submits
 * (extractors libvmaf skipped this frame, vmaf_sycl_graph_skip(), count as
 * done); earlier calls for the same frame are no-ops.
 *
 * @param state  The SYCL state.
 *
 * @return 0 on success, negative errno on failure.
 */
int vmaf_sycl_graph_submit(VmafSyclState *state);

/**
 * Tell the combined graph that libvmaf skips this extractor on the current
 * frame (n_subsample skips every extractor without the TEMPORAL or PREV_REF
 * flag). The frame's work is enqueued once every registered extractor has
 * either submitted or been skipped, and then runs only the callbacks of the
 * extractors that submitted. Without the call, a frame on which a registered
 * extractor never submits is never enqueued.
 *
 * @param state  The SYCL state.
 * @param priv   The extractor private-state pointer (fex->priv). A pointer no
 *               extractor registered with (an extractor on its own queue, or
 *               one not initialised yet) is not an error.
 *
 * @return 0 on success, negative errno on failure (an enqueue this call
 *         triggered failed, or state / priv is NULL).
 */
int vmaf_sycl_graph_skip(VmafSyclState *state, const void *priv);

/**
 * Wait for all GPU work to complete.
 * Idempotent per frame: once a wait for the frame has succeeded, later calls
 * return 0 without waiting. A failed wait does not count, so after a device
 * fault every collecting extractor's call waits again and fails, instead of
 * only the first.
 *
 * @param state  The SYCL state.
 *
 * @return 0 on success, negative errno on failure.
 */
int vmaf_sycl_graph_wait(VmafSyclState *state);

/**
 * Check whether combined command graphs have been recorded.
 *
 * @param state  The SYCL state.
 *
 * @return 1 if graphs are recorded, 0 otherwise.
 */
int vmaf_sycl_graphs_recorded(VmafSyclState *state);

/**
 * Wait unconditionally for the combined compute queue to drain.
 * Unlike vmaf_sycl_graph_wait(), this is NOT idempotent — it always waits.
 * Used by vmaf_sycl_wait_compute() to ensure previous frame's GPU extractors
 * have finished before the VA import path overwrites shared buffers.
 *
 * @param state  The SYCL state.
 *
 * @return 0 on success, negative errno on failure.
 */
int vmaf_sycl_combined_queue_wait(VmafSyclState *state);

/**
 * Advance the double-buffer slot and increment the frame counter.
 * Must be called once per frame in the zero-copy VA import path
 * (vmaf_read_pictures_sycl), because the host upload path
 * (vmaf_sycl_shared_frame_upload) does this internally but
 * the VA import path does not.
 *
 * Mirrors shared_frame_upload:597-599:
 *   cur_compute = cur_upload  (freshly imported slot → compute reads here)
 *   cur_upload  = 1-cur_upload (old compute slot → next import target)
 *   frame_counter++
 *
 * After this call, compute kernels (via vmaf_sycl_graph_submit) will
 * read from the slot that was just written by the VA import.
 *
 * Without this, graph_submit/graph_wait synchronization breaks:
 *   - graph_wait idempotency returns stale results from frame 2+
 *   - graph_submit fires after every extractor instead of once per frame
 *
 * @param state  The SYCL state.
 */
void vmaf_sycl_advance_frame(VmafSyclState *state);

/* ---- VA import deferred DMA-BUF free ---- */

/**
 * Free all pending DMA-BUF import allocations.
 * Called after the primary queue is drained (de-tile kernels finished)
 * so the imported device pointers are no longer read.
 *
 * @param state  The SYCL state.
 */
void vmaf_sycl_flush_pending_imports(VmafSyclState *state);

/**
 * Print timing summary (cpu/gpu breakdown) to stderr.
 */
void vmaf_sycl_print_timing(VmafSyclState *state);

/**
 * Host phases timed under VMAF_SYCL_TIMING=1 (average host ms per frame in the
 * `[vmaf-sycl] phases:` line). Plain C enum: no underlying type, C callers read it.
 */
enum VmafSyclPhase {
    VMAF_SYCL_PHASE_QUEUE_WAIT,
    VMAF_SYCL_PHASE_COMBINED_WAIT,
    VMAF_SYCL_PHASE_GRAPH_WAIT,
    VMAF_SYCL_PHASE_IMPORT,
    VMAF_SYCL_PHASE_COUNT
};

/**
 * Start a phase timer.
 *
 * @param state  The SYCL state.
 * @return Monotonic milliseconds, or 0 when VMAF_SYCL_TIMING is off or state is null.
 */
double vmaf_sycl_phase_start(const VmafSyclState *state);

/**
 * Add the time since `start_ms` to `phase`. No-op when timing is off or `start_ms` is 0.
 *
 * @param state     The SYCL state.
 * @param phase     The phase being closed.
 * @param start_ms  Value returned by vmaf_sycl_phase_start().
 */
void vmaf_sycl_phase_record(VmafSyclState *state, enum VmafSyclPhase phase, double start_ms);

/**
 * Defer a DMA-BUF import pointer for freeing on the next queue wait.
 * The pointer will be freed by vmaf_sycl_flush_pending_imports().
 *
 * @param state  The SYCL state.
 * @param ptr    Device pointer from vmaf_sycl_dmabuf_import().
 */
void vmaf_sycl_defer_import_free(VmafSyclState *state, void *ptr);

/**
 * Record the last de-tile kernel event for cross-queue synchronization.
 * The compute queue will barrier on this event before running extractors.
 *
 * @param state      The SYCL state.
 * @param event_ptr  Pointer to sycl::event from the de-tile parallel_for.
 */
void vmaf_sycl_set_detile_event(VmafSyclState *state, void *event_ptr);

/**
 * Internal, used by core/test only; not part of the public API. Run the Tile4
 * luma de-tile kernel of the zero-copy import on one plane at offset 0 of
 * `src_tiled` and wait for it.
 *
 * @param state      The SYCL state.
 * @param dst        Device buffer of `row_bytes * h` bytes (linear output).
 * @param src_tiled  Device buffer holding the Tile4 plane.
 * @param pitch      Tiled pitch in bytes, a multiple of 128.
 * @param row_bytes  Bytes of one output row, at most `pitch`.
 * @param h          Rows.
 * @param bpc        Bits per component; above 8 the samples are shifted
 *                   right by 16 - bpc as the import does for P010 / P012.
 *
 * @return 0, -EINVAL on a bad argument, -EIO when the kernel failed, -ENOSYS
 *         on a build without the DMA-BUF import.
 */
int vmaf_sycl_detile_tile4_for_test(VmafSyclState *state, void *dst, const void *src_tiled,
                                    size_t pitch, size_t row_bytes, unsigned h, unsigned bpc);

/* ---- Profiling ---- */

/**
 * Record a kernel timing entry (accumulated per kernel name).
 *
 * @param state        The SYCL state.
 * @param kernel_name  Human-readable kernel name.
 * @param delta_ns     Elapsed time in nanoseconds.
 */
void vmaf_sycl_profiling_record(VmafSyclState *state, const char *kernel_name, uint64_t delta_ns);

/**
 * Check whether profiling is enabled.
 *
 * @param state  The SYCL state.
 *
 * @return true if profiling is enabled.
 */
bool vmaf_sycl_profiling_is_enabled(const VmafSyclState *state);

#ifdef __cplusplus
}
#endif

#endif /* HAVE_SYCL */

#endif /* VMAF_SRC_SYCL_COMMON_H_ */
