/**
 *
 *  Copyright 2016-2023 Netflix, Inc.
 *
 *     Licensed under the BSD+Patent License (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         https://opensource.org/licenses/BSDplusPatent
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */

#ifndef IQA_SSIM_SIMD_H_
#define IQA_SSIM_SIMD_H_

typedef void (*ssim_precompute_fn)(const float *ref, const float *cmp, float *ref_sq, float *cmp_sq,
                                   float *ref_cmp, int n);

typedef void (*ssim_variance_fn)(float *ref_sigma_sqd, float *cmp_sigma_sqd, float *sigma_both,
                                 const float *ref_mu, const float *cmp_mu, int n);

typedef void (*ssim_accumulate_fn)(const float *ref_mu, const float *cmp_mu,
                                   const float *ref_sigma_sqd, const float *cmp_sigma_sqd,
                                   const float *sigma_both, int n, float C1, float C2, float C3,
                                   double *ssim_sum, double *l_sum, double *c_sum, double *s_sum);

void iqa_ssim_set_dispatch(ssim_precompute_fn precompute, ssim_variance_fn variance,
                           ssim_accumulate_fn accumulate);

/*
 * Fork-local addition: SIMD dispatch for `iqa_convolve` (the 1-D
 * separable, interior-only fast path under IQA_CONVOLVE_1D). The
 * signature is primitive — decouples SIMD translation units from
 * iqa/convolve.h `struct iqa_kernel`. ssim_tools.c adapts the struct
 * to these primitive args at each call site.
 *
 * `workspace` is a caller-owned `w*h`-float scratch buffer used by the
 * horizontal pass. NULL is accepted — the kernel allocates internally
 * (kept for standalone unit tests). The hot path in `iqa_ssim`
 * allocates once and reuses across all 5 dispatch sites, eliminating
 * ~1200 calloc/free pairs per 120-frame run at 1080p. See
 * docs/adr/0138-iqa-convolve-avx2-bitexact-double.md.
 */
typedef void (*iqa_convolve_fn)(float *img, int w, int h, const float *kernel_h,
                                const float *kernel_v, int kw, int kh, int normalized,
                                float *workspace, float *result, int *rw, int *rh);

void iqa_convolve_set_dispatch(iqa_convolve_fn convolve);

#endif /* IQA_SSIM_SIMD_H_ */
