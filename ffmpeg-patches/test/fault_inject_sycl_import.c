/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * LD_PRELOAD fault injector for vmaf_sycl_import_va_surface(), used by
 * ffmpeg-patches/test/check-sycl-import-retry.sh against an FFmpeg that links
 * libvmaf.so dynamically. It makes calls number
 * VMAF_TEST_IMPORT_FAIL_AT .. VMAF_TEST_IMPORT_FAIL_AT + VMAF_TEST_IMPORT_FAIL_COUNT - 1
 * (1-based, counted over every call, reference and distorted) return -EIO
 * without importing, which is what the real call returns when a VA surface
 * cannot be read, and forwards every other call to libvmaf. Unset variables
 * inject nothing. Build with -D_GNU_SOURCE (RTLD_NEXT) against the installed
 * libvmaf headers, as the script does.
 */

#include <dlfcn.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include <libvmaf/libvmaf_sycl.h>

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit; MSVC's C mode has
 * no `nullptr`. ADR-1138. */

typedef int (*import_fn)(VmafSyclState *state, void *va_display, unsigned int va_surface,
                         int is_ref, unsigned w, unsigned h, unsigned bpc);

static long env_long(const char *name)
{
    /* NOLINTNEXTLINE(concurrency-mt-unsafe) — test shim; FFmpeg's filter thread is the only caller. */
    const char *v = getenv(name);
    if (v == NULL || *v == '\0') {
        return 0;
    }
    char *end = NULL;
    const long n = strtol(v, &end, 10);
    return (end != NULL && *end == '\0' && n > 0) ? n : 0;
}

static atomic_long call_count;

int vmaf_sycl_import_va_surface(VmafSyclState *state, void *va_display, unsigned int va_surface,
                                int is_ref, unsigned w, unsigned h, unsigned bpc)
{
    const long call = atomic_fetch_add(&call_count, 1) + 1;
    const long fail_at = env_long("VMAF_TEST_IMPORT_FAIL_AT");
    const long fail_count = env_long("VMAF_TEST_IMPORT_FAIL_COUNT");
    if (fail_at > 0 && call >= fail_at && call < fail_at + fail_count) {
        return -EIO;
    }
    /* POSIX: a dlsym() result converts to a function pointer; memcpy keeps ISO C quiet. */
    void *sym = dlsym(RTLD_NEXT, "vmaf_sycl_import_va_surface");
    if (sym == NULL) {
        return -ENOSYS;
    }
    import_fn real = NULL;
    memcpy((void *)&real, (const void *)&sym, sizeof(real));
    return real(state, va_display, va_surface, is_ref, w, h, bpc);
}

/* NOLINTEND(modernize-use-nullptr) */
