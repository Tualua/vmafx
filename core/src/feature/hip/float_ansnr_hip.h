/**
 *  Copyright 2026 Lusoris and Claude (Anthropic)
 *  SPDX-License-Identifier: BSD-3-Clause-Plus-Patent
 *
 *  HIP host glue for the float_ansnr feature extractor — fifth
 *  kernel-template consumer (T7-10b follow-up / ADR-0266;
 *  real kernel: T7-10b batch-1 / ADR-0372).
 *
 *  Mirrors libvmaf/src/feature/cuda/float_ansnr_cuda.h. The HSACO
 *  fat binary (`float_ansnr_score.hip` compiled via `hipcc --genco`,
 *  embedded by `xxd -i`) is declared here when `HAVE_HIPCC` is defined.
 */
#ifndef FEATURE_FLOAT_ANSNR_HIP_H_
#define FEATURE_FLOAT_ANSNR_HIP_H_

#ifdef HAVE_HIPCC
/*
 * Symbol produced by
 * `xxd -i float_ansnr_score.hsaco > float_ansnr_score_hsaco.c`
 * in the meson `hip_hsaco_sources` custom_target pipeline (ADR-0372).
 * Mirrors `float_ansnr_score_ptx` in
 * `libvmaf/src/feature/cuda/float_ansnr_cuda.h`.
 */
extern const unsigned char float_ansnr_score_hsaco[];
extern const unsigned int float_ansnr_score_hsaco_len;
#endif /* HAVE_HIPCC */

#endif /* FEATURE_FLOAT_ANSNR_HIP_H_ */
