/**
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: EUPL-1.2
 */

#ifndef VMAF_COMPAT_CRT_PORTABLE_H_
#define VMAF_COMPAT_CRT_PORTABLE_H_

/*
 * The C runtime calls the Windows CRT deprecates (strdup, close, sscanf,
 * getenv) under the name the Windows CRT itself recommends, and the plain POSIX
 * name everywhere else. Using the CRT's own replacement keeps the Windows build
 * free of C4996 without _CRT_SECURE_NO_WARNINGS or a pragma.
 *
 *   VMAF_STRDUP(s)        strdup(); _strdup() on Windows
 *   VMAF_CLOSE(fd)        close(); _close() on Windows
 *   VMAF_SSCANF(...)      sscanf(); sscanf_s() under MSVC. Only for formats
 *                         without %s, %c or %[ (sscanf_s takes a size for those).
 *   vmaf_getenv_portable  getenv(); getenv_s() into a per-thread buffer under
 *                         MSVC. The result is valid until the next call on the
 *                         same thread; NULL when the variable is unset or does
 *                         not fit the 4096-byte buffer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define VMAF_STRDUP _strdup
#define VMAF_CLOSE _close
#else
#include <unistd.h>
#define VMAF_STRDUP strdup
#define VMAF_CLOSE close
#endif

#if defined(_MSC_VER)
#define VMAF_SSCANF sscanf_s
#else
#define VMAF_SSCANF sscanf
#endif

#ifdef __cplusplus
#define VMAF_CRT_THREAD_LOCAL thread_local
#else
#define VMAF_CRT_THREAD_LOCAL _Thread_local
#endif

#define VMAF_GETENV_BUF 4096

static inline const char *vmaf_getenv_portable(const char *name)
{
#if defined(_MSC_VER)
    static VMAF_CRT_THREAD_LOCAL char buf[VMAF_GETENV_BUF];
    size_t required = 0;
    if (getenv_s(&required, buf, sizeof(buf), name) != 0 || required == 0) {
        return NULL;
    }
    return buf;
#else
    /* NOLINTNEXTLINE(concurrency-mt-unsafe) — callers keep their own ADR-0488 / ADR-1155 contract. */
    return getenv(name);
#endif
}

#endif /* VMAF_COMPAT_CRT_PORTABLE_H_ */
