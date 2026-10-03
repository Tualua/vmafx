/**
 *  Copyright 2016-2026 Netflix, Inc.
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef FEATURE_FLOAT_PSNR_CUDA_H_
#define FEATURE_FLOAT_PSNR_CUDA_H_

#include <stdint.h>

#include "common.h"

extern const unsigned char float_psnr_score_ptx[];

/* Launch geometry shared by float_psnr_score.cu and its host (ADR-1499): one
 * block per FPSNR_BX pixels of one row, so that each block sum is a segment
 * of a single row and the host can form each row's exact sum. */
#define FPSNR_BX 256u
#define FPSNR_BY 1u

#endif /* FEATURE_FLOAT_PSNR_CUDA_H_ */
