/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * The kernels of core/src/feature/metal/integer_adm.metal run on the host
 * (ADR-1806): metal_integer_adm_host_replay_kernels.cpp compiles the
 * unmodified kernel file through core/test/metal_msl_host_shim.h and runs one
 * dispatch of integer_adm_metal_host.c's plan the way integer_adm_metal.mm
 * encodes it, with the same buffers bound at the same indices. Every
 * threadgroup runs as one thread.
 */

#ifndef LIBVMAF_TEST_METAL_INTEGER_ADM_HOST_REPLAY_H_
#define LIBVMAF_TEST_METAL_INTEGER_ADM_HOST_REPLAY_H_

#include <stdint.h>

#include "feature/metal/integer_adm_metal_host.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The device buffers of one frame, each sized by iadm_metal_buffer_bytes(),
 * as integer_adm_metal.mm holds them. */
/* NOLINTBEGIN(modernize-use-using): C header included by C and C++ translation
 * units; C has no `using`. ADR-0141. */
typedef struct IadmReplayBuffers {
    uint8_t *src_ref;
    uint8_t *src_dis;
    uint8_t *dwt_tmp_ref;
    uint8_t *dwt_tmp_dis;
    uint8_t *ref_band[IADM_METAL_NUM_SCALES];
    uint8_t *dis_band[IADM_METAL_NUM_SCALES];
    uint8_t *csf_a;
    uint8_t *csf_f;
    uint8_t *accum[IADM_METAL_NUM_SCALES];
} IadmReplayBuffers;
/* NOLINTEND(modernize-use-using) */

/* Runs `stage` of `scale`: every thread position of its grid, one thread per
 * threadgroup. */
void iadm_replay_stage(const IadmMetalStage *stage, int scale, const IadmDims *d, const IadmCsf *c,
                       const IadmReplayBuffers *b);

#ifdef __cplusplus
}
#endif

#endif /* LIBVMAF_TEST_METAL_INTEGER_ADM_HOST_REPLAY_H_ */
