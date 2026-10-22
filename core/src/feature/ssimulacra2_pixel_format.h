/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * The pixel formats SSIMULACRA 2 can score, in one place for the CPU
 * extractor and every twin (CUDA, SYCL, HIP, Metal).
 *
 * The colour conversion reads the U and V planes. A 4:0:0 picture has neither
 * (picture.c leaves data[1] and data[2] NULL and their sizes 0), and an
 * unknown format says nothing about them, so init() refuses both before it
 * allocates anything. A converter that is handed such a picture reads through
 * a NULL plane: the CPU extractor did until 2026-10-01, the Metal twin until
 * T-METAL-SSIMULACRA2-YUV400-ACCEPTED-2026-10-05.
 *
 * Host code only: ssimulacra2_score.h is also compiled as CUDA and HIP device
 * code and cannot call vmaf_log().
 */

#ifndef VMAF_FEATURE_SSIMULACRA2_PIXEL_FORMAT_H_
#define VMAF_FEATURE_SSIMULACRA2_PIXEL_FORMAT_H_

#include <errno.h>
#include <stdbool.h>

#include "libvmaf/picture.h"
#include "log.h"

/* True when the picture has the U and V planes the colour conversion reads.
 * The ADR-1324 context checks of the twins use it to send the other formats
 * to the CPU extractor, which refuses them with the message below. */
static inline bool vmaf_ss2_has_chroma(enum VmafPixelFormat pix_fmt)
{
    return pix_fmt != VMAF_PIX_FMT_YUV400P && pix_fmt != VMAF_PIX_FMT_UNKNOWN;
}

/* init()'s refusal: 0 for 4:2:0, 4:2:2 and 4:4:4, else -EINVAL with
 * "<extractor>: needs a YUV 4:2:0, 4:2:2 or 4:4:4 input, not 4:0:0". */
static inline int vmaf_ss2_check_pixel_format(enum VmafPixelFormat pix_fmt, const char *extractor)
{
    if (vmaf_ss2_has_chroma(pix_fmt))
        return 0;
    vmaf_log(VMAF_LOG_LEVEL_ERROR, "%s: needs a YUV 4:2:0, 4:2:2 or 4:4:4 input, not 4:0:0\n",
             extractor);
    return -EINVAL;
}

#endif /* VMAF_FEATURE_SSIMULACRA2_PIXEL_FORMAT_H_ */
