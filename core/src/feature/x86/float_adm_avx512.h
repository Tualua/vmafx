/**
 *
 *  Copyright 2016-2026 Netflix, Inc.
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
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

#ifndef X86_AVX512_FLOAT_ADM_H_
#define X86_AVX512_FLOAT_ADM_H_

#include "../adm_tools.h"

/* adm_dwt2_s() for AVX-512: the same bits, the same return value. */
int float_adm_dwt2_avx512(const float *src, const adm_dwt_band_t_s *dst, int **ind_y, int **ind_x,
                          int w, int h, int src_stride, int dst_stride);

/* adm_csf_plane_s() for AVX-512 (an adm_csf_plane_fn): the same bits. */
void float_adm_csf_avx512(const float *src, float *dst, float *flt, int w, int h, int src_stride,
                          int dst_stride, float factor, double one_by_30);

#endif /* X86_AVX512_FLOAT_ADM_H_ */
