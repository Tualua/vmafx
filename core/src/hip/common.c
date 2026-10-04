/**
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 *  HIP backend common surface — runtime PR (T7-10b / ADR-0212
 *  §"What lands next" steps 1+2).
 *
 *  Replaces the audit-first `-ENOSYS` stubs with real ROCm HIP
 *  runtime calls. Mirrors core/src/vulkan/common.c.
 *
 *    - `vmaf_hip_device_count`  -> hipGetDeviceCount (a runtime failure
 *                                  is a negative errno, not a count)
 *    - `vmaf_hip_context_new`   -> checks the index against the count,
 *                                  then hipSetDevice
 *    - `vmaf_hip_state_init`    -> the same device selection +
 *                                  hipStreamCreateWithFlags
 *    - `vmaf_hip_state_free`    -> hipStreamDestroy + free
 *    - `vmaf_hip_list_devices`  -> hipGetDeviceCount +
 *                                  hipGetDeviceProperties (logs one
 *                                  line per device, returns count)
 *
 *  `vmaf_hip_import_state` lives in `core/src/libvmaf.c` (next to
 *  the CUDA / SYCL / Vulkan / Metal `_import_state` twins) because it
 *  needs `VmafContext` field-level access to stash the borrowed state
 *  pointer. Removed from this TU by ADR-0519.
 */

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include <hip/hip_runtime_api.h>

#include "common.h"
#include "hip_handle.h"
#include "log.h"

#include "libvmaf/libvmaf_hip.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy also proposes the `nullptr` keyword, but this is a C
 * translation unit whose sources spell the null pointer constant `NULL` and
 * MSVC's documented /std:clatest C23 feature set does not include `nullptr`
 * while the required Windows build compiles this TU with cl.exe. ADR-1138. */

struct VmafHipContext {
    int device_index;
    /* hipStream_t handle stashed as uintptr_t for header purity (the
     * public `common.h` stays free of `<hip/hip_runtime.h>`). */
    uintptr_t stream;
};

/* Public-API wrapper. Same lifetime model as `VmafCudaState`: caller
 * owns the allocation, libvmaf borrows it for the duration of an
 * imported VmafContext. */
struct VmafHipState {
    struct VmafHipContext ctx;
};

/* Make `device_index` the calling thread's HIP device, after checking that
 * the runtime has a device by that index. -ENODEV when it has none, -EINVAL
 * for an index outside [0, count), the runtime's error otherwise. */
static int vmaf_hip_select_device(int device_index)
{
    const int n = vmaf_hip_device_count();
    if (n < 0) {
        return n;
    }
    if (n == 0) {
        return -ENODEV;
    }
    assert(n > 0);
    if (device_index < 0 || device_index >= n) {
        return -EINVAL;
    }
    /* NASA P10 r5: the invariant the device selection below depends on. */
    assert(device_index >= 0 && device_index < n);
    return vmaf_hip_rc_to_errno(hipSetDevice(device_index));
}

int vmaf_hip_context_new(VmafHipContext **out, int device_index)
{
    if (out == NULL) {
        return -EINVAL;
    }
    *out = NULL;
    const int err = vmaf_hip_select_device(device_index);
    if (err) {
        return err;
    }
    VmafHipContext *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return -ENOMEM;
    }
    ctx->device_index = device_index;
    ctx->stream = 0;
    *out = ctx;
    return 0;
}

void vmaf_hip_context_destroy(VmafHipContext *ctx)
{
    if (ctx == NULL) {
        return;
    }
    if (ctx->stream != 0) {
        (void)hipStreamDestroy(vmaf_hip_stream_of(ctx->stream));
        ctx->stream = 0;
    }
    free(ctx);
}

int vmaf_hip_device_count(void)
{
    int n = 0;
    const hipError_t rc = hipGetDeviceCount(&n);
    if (rc == hipErrorNoDevice) {
        /* The runtime answered and sees no device (an empty or masked
         * HIP_VISIBLE_DEVICES, no /dev/kfd): a count of 0, not a failure. */
        return 0;
    }
    if (rc != hipSuccess) {
        /* The runtime could not be asked (driver, initialisation): an error,
         * never a count, so a caller cannot read it as "no device". */
        return vmaf_hip_rc_to_errno(rc);
    }
    return n;
}

int vmaf_hip_state_device_index(const VmafHipState *state)
{
    if (state == NULL) {
        return -EINVAL;
    }
    return state->ctx.device_index;
}

int vmaf_hip_state_bind(const VmafHipState *state)
{
    if (state == NULL) {
        return -EINVAL;
    }
    return vmaf_hip_rc_to_errno(hipSetDevice(state->ctx.device_index));
}

/* ---- Public C-API entry points (libvmaf_hip.h) ---- */

int vmaf_hip_available(void)
{
#ifdef HAVE_HIP
    return 1;
#else
    return 0;
#endif
}

int vmaf_hip_state_init(VmafHipState **out, VmafHipConfiguration cfg)
{
    if (out == NULL) {
        return -EINVAL;
    }
    /* NASA P10 r5: pin the post-validation invariants the rest of
     * the function depends on. */
    assert(out != NULL);
    *out = NULL;

    /* -1 selects the first device. */
    const int device_index = (cfg.device_index < 0) ? 0 : cfg.device_index;
    const int err = vmaf_hip_select_device(device_index);
    if (err) {
        return err;
    }
    assert(device_index >= 0);

    VmafHipState *s = calloc(1, sizeof(*s));
    if (s == NULL) {
        return -ENOMEM;
    }
    s->ctx.device_index = device_index;

    hipStream_t stream = NULL;
    const hipError_t hip_rc = hipStreamCreateWithFlags(&stream, hipStreamNonBlocking);
    if (hip_rc != hipSuccess) {
        free(s);
        return -EIO;
    }
    s->ctx.stream = (uintptr_t)stream;
    *out = s;
    return 0;
}

/* vmaf_hip_import_state moved to core/src/libvmaf.c — needs
 * VmafContext field-level access. ADR-0519. */

void vmaf_hip_state_free(VmafHipState **state)
{
    if (state == NULL || *state == NULL) {
        return;
    }
    VmafHipState *s = *state;
    if (s->ctx.stream != 0) {
        (void)hipStreamDestroy(vmaf_hip_stream_of(s->ctx.stream));
        s->ctx.stream = 0;
    }
    free(s);
    *state = NULL;
}

int vmaf_hip_list_devices(void)
{
    const int n = vmaf_hip_device_count();
    if (n < 0) {
        return n;
    }
    assert(n >= 0);
    for (int i = 0; i < n; ++i) {
        hipDeviceProp_t prop;
        const hipError_t rc = hipGetDeviceProperties(&prop, i);
        if (rc != hipSuccess) {
            /* A device that cannot be described is reported, not skipped:
             * the count would otherwise name devices the log never showed. */
            return vmaf_hip_rc_to_errno(rc);
        }
        vmaf_log(VMAF_LOG_LEVEL_INFO, "HIP device %d: %s (arch %s)\n", i, prop.name,
                 prop.gcnArchName);
    }
    assert(n >= 0);
    return n;
}

/* NOLINTEND(modernize-use-nullptr) */
