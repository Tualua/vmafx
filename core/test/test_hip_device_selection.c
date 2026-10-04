/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * HIP device selection without a device (core/src/hip/common.c).
 *
 * common.c is compiled straight into this target against the stubs below
 * instead of the ROCm runtime: a fake runtime with a chosen device count, a
 * chosen error from hipGetDeviceCount() / hipSetDevice() /
 * hipGetDeviceProperties(), and a record of the device last selected. That
 * makes the contract of common.h checkable on any host, the hosted HIP lane
 * without a GPU included:
 *
 *   - vmaf_hip_context_new() creates its context on the device it is given
 *     and refuses an index the runtime does not have; it used to store the
 *     index and select nothing, so every HIP twin ran on whatever device the
 *     calling thread happened to have;
 *   - vmaf_hip_device_count() reports a runtime failure as a negative errno;
 *     it used to report it as a count of 0, indistinguishable from a host
 *     without a device;
 *   - vmaf_hip_list_devices() and vmaf_hip_state_init() report the same
 *     failures, and the state remembers and rebinds its device.
 *
 * vmaf_hip_rc_to_errno() is stubbed to a value no real mapping returns, so
 * every assertion below also checks that the runtime's own code is what
 * reaches the caller.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <hip/hip_runtime_api.h>

#include "test.h"

#include "hip/common.h"
#include "hip/hip_handle.h"
#include "libvmaf/libvmaf_hip.h"
#include "log.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy also proposes the `nullptr` keyword, but MSVC's
 * documented /std:clatest C23 feature set does not include `nullptr`, and
 * this file mirrors the C spelling of the surface it stands in for.
 * ADR-1138. */

/* ------------------------------------------------------------------ */
/* The runtime, as far as common.c uses it                              */
/* ------------------------------------------------------------------ */

/* The errno a stubbed runtime code maps to: distinct from every real errno,
 * so a test can tell the runtime's code from one common.c made up. */
#define STUB_ERRNO(rc) (-(1000 + (int)(rc)))

static int g_device_count;
static hipError_t g_count_rc;
static hipError_t g_set_device_rc;
static hipError_t g_properties_rc;
static int g_current_device;
static unsigned g_set_device_calls;
static unsigned g_streams_live;

int vmaf_hip_rc_to_errno(hipError_t rc)
{
    return (rc == hipSuccess) ? 0 : STUB_ERRNO(rc);
}

hipError_t hipGetDeviceCount(int *count)
{
    *count = (g_count_rc == hipSuccess) ? g_device_count : 0;
    return g_count_rc;
}

hipError_t hipSetDevice(int device_id)
{
    g_set_device_calls++;
    if (g_set_device_rc != hipSuccess)
        return g_set_device_rc;
    g_current_device = device_id;
    return hipSuccess;
}

hipError_t hipGetDeviceProperties(hipDeviceProp_t *prop, int device_id)
{
    (void)device_id;
    if (g_properties_rc != hipSuccess)
        return g_properties_rc;
    (void)memset(prop, 0, sizeof(*prop));
    return hipSuccess;
}

/* A stream is an address the stubs never dereference. */
static unsigned char g_stream_token;

hipError_t hipStreamCreateWithFlags(hipStream_t *stream, unsigned int flags)
{
    (void)flags;
    g_streams_live++;
    *stream = vmaf_hip_stream_of((uintptr_t)&g_stream_token);
    return hipSuccess;
}

hipError_t hipStreamDestroy(hipStream_t stream)
{
    (void)stream;
    g_streams_live--;
    return hipSuccess;
}

void vmaf_log(enum VmafLogLevel log_level, const char *fmt, ...)
{
    (void)log_level;
    (void)fmt;
}

/* A runtime with `count` devices and no failure; device 0 current. */
static void fake_runtime(int count)
{
    g_device_count = count;
    g_count_rc = hipSuccess;
    g_set_device_rc = hipSuccess;
    g_properties_rc = hipSuccess;
    g_current_device = 0;
    g_set_device_calls = 0u;
}

/* ------------------------------------------------------------------ */
/* vmaf_hip_device_count()                                              */
/* ------------------------------------------------------------------ */

static char *test_device_count_reports_the_runtime_count(void)
{
    fake_runtime(3);
    mu_assert("three devices are counted as three", vmaf_hip_device_count() == 3);
    return NULL;
}

/* hipErrorNoDevice is how the runtime answers "none" (HIP_VISIBLE_DEVICES
 * masking every device, or no /dev/kfd): a count, not a failure. */
static char *test_device_count_no_device_is_zero(void)
{
    fake_runtime(0);
    g_count_rc = hipErrorNoDevice;
    mu_assert("no device is a count of 0", vmaf_hip_device_count() == 0);
    return NULL;
}

static char *test_device_count_runtime_failure_is_an_errno(void)
{
    fake_runtime(2);
    g_count_rc = hipErrorInsufficientDriver;
    const int n = vmaf_hip_device_count();
    mu_assert("a runtime failure must not read as a count", n < 0);
    mu_assert("the runtime's code reaches the caller", n == STUB_ERRNO(hipErrorInsufficientDriver));
    return NULL;
}

/* ------------------------------------------------------------------ */
/* vmaf_hip_context_new()                                               */
/* ------------------------------------------------------------------ */

static char *test_context_new_selects_the_given_device(void)
{
    fake_runtime(2);
    VmafHipContext *ctx = NULL;
    mu_assert("device 1 of 2 is accepted", vmaf_hip_context_new(&ctx, 1) == 0 && ctx != NULL);
    mu_assert("device 1 is the thread's device afterwards", g_current_device == 1);
    vmaf_hip_context_destroy(ctx);

    mu_assert("device 0 of 2 is accepted", vmaf_hip_context_new(&ctx, 0) == 0 && ctx != NULL);
    mu_assert("device 0 is the thread's device afterwards", g_current_device == 0);
    vmaf_hip_context_destroy(ctx);
    return NULL;
}

/* The last valid index and the first invalid one on each side. */
static char *test_context_new_refuses_an_index_out_of_range(void)
{
    fake_runtime(2);
    VmafHipContext *last = NULL;
    mu_assert("index count - 1 is accepted", vmaf_hip_context_new(&last, 1) == 0 && last != NULL);
    g_set_device_calls = 0u;

    /* A refusal must not leave the caller's previous pointer in place. */
    VmafHipContext *ctx = last;
    const int beyond = vmaf_hip_context_new(&ctx, 2);
    vmaf_hip_context_destroy(last);
    mu_assert("index == count is refused", beyond == -EINVAL);
    mu_assert("a refused context is NULL", ctx == NULL);
    mu_assert("index -1 is refused", vmaf_hip_context_new(&ctx, -1) == -EINVAL);
    mu_assert("no device is selected for a refused index", g_set_device_calls == 0u);
    return NULL;
}

static char *test_context_new_without_a_device(void)
{
    fake_runtime(0);
    g_count_rc = hipErrorNoDevice;
    VmafHipContext *ctx = NULL;
    mu_assert("no device -> -ENODEV", vmaf_hip_context_new(&ctx, 0) == -ENODEV);
    mu_assert("no context without a device", ctx == NULL);
    return NULL;
}

static char *test_context_new_reports_runtime_failures(void)
{
    fake_runtime(2);
    g_count_rc = hipErrorInsufficientDriver;
    VmafHipContext *ctx = NULL;
    mu_assert("a count failure reaches the caller",
              vmaf_hip_context_new(&ctx, 0) == STUB_ERRNO(hipErrorInsufficientDriver));
    mu_assert("no context on a count failure", ctx == NULL);

    fake_runtime(2);
    g_set_device_rc = hipErrorInvalidDevice;
    mu_assert("a hipSetDevice failure reaches the caller",
              vmaf_hip_context_new(&ctx, 1) == STUB_ERRNO(hipErrorInvalidDevice));
    mu_assert("no context on a selection failure", ctx == NULL);
    return NULL;
}

static char *test_context_new_rejects_null_out(void)
{
    fake_runtime(1);
    mu_assert("NULL out -> -EINVAL", vmaf_hip_context_new(NULL, 0) == -EINVAL);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* vmaf_hip_list_devices(), vmaf_hip_state_*()                          */
/* ------------------------------------------------------------------ */

static char *test_list_devices_reports_failures(void)
{
    fake_runtime(2);
    mu_assert("two devices are listed", vmaf_hip_list_devices() == 2);

    g_properties_rc = hipErrorInvalidDevice;
    mu_assert("a device that cannot be described is an error",
              vmaf_hip_list_devices() == STUB_ERRNO(hipErrorInvalidDevice));

    fake_runtime(2);
    g_count_rc = hipErrorInsufficientDriver;
    mu_assert("a runtime failure is an error, not 0 devices",
              vmaf_hip_list_devices() == STUB_ERRNO(hipErrorInsufficientDriver));
    return NULL;
}

static char *test_state_remembers_and_binds_its_device(void)
{
    fake_runtime(2);
    VmafHipState *state = NULL;
    const VmafHipConfiguration second = {.device_index = 1, .flags = 0};
    mu_assert("state on device 1", vmaf_hip_state_init(&state, second) == 0 && state != NULL);
    mu_assert("the state reports device 1", vmaf_hip_state_device_index(state) == 1);

    /* Another thread or context moved the device; binding restores it. */
    g_current_device = 0;
    const int bound = vmaf_hip_state_bind(state);
    vmaf_hip_state_free(&state);
    mu_assert("bind succeeds", bound == 0);
    mu_assert("bind selects the state's device", g_current_device == 1);
    mu_assert("the state's stream is released", g_streams_live == 0u);
    return NULL;
}

static char *test_state_default_device_and_null(void)
{
    fake_runtime(2);
    VmafHipState *state = NULL;
    const VmafHipConfiguration first = {.device_index = -1, .flags = 0};
    const int created = vmaf_hip_state_init(&state, first);
    const int device = vmaf_hip_state_device_index(state);
    vmaf_hip_state_free(&state);
    mu_assert("-1 picks device 0", created == 0 && device == 0);
    mu_assert("NULL state has no device", vmaf_hip_state_device_index(NULL) == -EINVAL);
    mu_assert("NULL state cannot be bound", vmaf_hip_state_bind(NULL) == -EINVAL);
    return NULL;
}

static char *test_state_init_reports_failures(void)
{
    VmafHipState *state = NULL;
    fake_runtime(2);
    const VmafHipConfiguration third = {.device_index = 2, .flags = 0};
    mu_assert("device 2 of 2 is refused", vmaf_hip_state_init(&state, third) == -EINVAL);

    fake_runtime(0);
    g_count_rc = hipErrorNoDevice;
    const VmafHipConfiguration first = {.device_index = -1, .flags = 0};
    mu_assert("no device -> -ENODEV", vmaf_hip_state_init(&state, first) == -ENODEV);

    fake_runtime(2);
    g_count_rc = hipErrorInsufficientDriver;
    mu_assert("a runtime failure is not reported as a missing device",
              vmaf_hip_state_init(&state, first) == STUB_ERRNO(hipErrorInsufficientDriver));
    mu_assert("no state on a failure", state == NULL);
    return NULL;
}

static char *run_count_tests(void)
{
    mu_run_test(test_device_count_reports_the_runtime_count);
    mu_run_test(test_device_count_no_device_is_zero);
    mu_run_test(test_device_count_runtime_failure_is_an_errno);
    return NULL;
}

static char *run_context_tests(void)
{
    mu_run_test(test_context_new_selects_the_given_device);
    mu_run_test(test_context_new_refuses_an_index_out_of_range);
    mu_run_test(test_context_new_without_a_device);
    mu_run_test(test_context_new_reports_runtime_failures);
    mu_run_test(test_context_new_rejects_null_out);
    return NULL;
}

static char *run_list_and_state_tests(void)
{
    mu_run_test(test_list_devices_reports_failures);
    mu_run_test(test_state_remembers_and_binds_its_device);
    mu_run_test(test_state_default_device_and_null);
    mu_run_test(test_state_init_reports_failures);
    return NULL;
}

char *run_tests(void)
{
    mu_assert_msg(run_count_tests());
    mu_assert_msg(run_context_tests());
    mu_assert_msg(run_list_and_state_tests());
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
