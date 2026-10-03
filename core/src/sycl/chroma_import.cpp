/**
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: EUPL-1.2
 *
 *  RED stub for ADR-1597: every entry point reports -ENOSYS until the kernel
 *  lands in the next commit.
 */
#include "config.h"

#if HAVE_SYCL

#include <cerrno>

#include "chroma_import.h"

extern "C" int vmaf_sycl_chroma_layout_from_modifier(uint64_t modifier,
                                                     enum VmafSyclChromaLayout *out)
{
    (void)modifier;
    (void)out;
    return -ENOSYS;
}

extern "C" int vmaf_sycl_chroma_src_validate(const VmafSyclChromaSrc *src, size_t object_size)
{
    (void)src;
    (void)object_size;
    return -ENOSYS;
}

extern "C" int vmaf_sycl_chroma_import_launch(VmafSyclState *state, const VmafSyclChromaSrc *src,
                                              size_t object_size, void *dst_cb, void *dst_cr,
                                              void **out_event)
{
    (void)state;
    (void)src;
    (void)object_size;
    (void)dst_cb;
    (void)dst_cr;
    (void)out_event;
    return -ENOSYS;
}

extern "C" void vmaf_sycl_chroma_event_free(void *event)
{
    (void)event;
}

#endif /* HAVE_SYCL */
