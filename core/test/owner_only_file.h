/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * A fixture file a test writes, created owner-only: POSIX mode 0600 from the
 * first open, so no umask can leave it world-writable (CodeQL
 * cpp/world-writable-file-creation); the Windows C runtime has no wider mode
 * than read and write for the owner. Shared by every test that writes a file
 * (HISS-19: one helper, not one copy per test).
 */

#ifndef VMAF_TEST_OWNER_ONLY_FILE_H_
#define VMAF_TEST_OWNER_ONLY_FILE_H_

#include <stdio.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

/* NOLINTBEGIN(modernize-use-nullptr): C header for C translation units; MSVC's
 * /std:clatest C23 feature set has no `nullptr` (ADR-1138). */

/* `path` truncated and opened for binary writing, owner-only; NULL on error. */
static inline FILE *vmaf_test_open_owner_only(const char *path)
{
#ifdef _WIN32
    const int fd = _open(path, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);
    return fd < 0 ? NULL : _fdopen(fd, "wb");
#else
    const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd < 0)
        return NULL;
    FILE *out = fdopen(fd, "wb");
    if (!out)
        (void)close(fd);
    return out;
#endif
}

/* NOLINTEND(modernize-use-nullptr) */

#endif /* VMAF_TEST_OWNER_ONLY_FILE_H_ */
