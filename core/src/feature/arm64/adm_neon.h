/**
 *
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

#ifndef ARM_64_ADM_H_
#define ARM_64_ADM_H_

#include "feature/integer_adm.h"

void adm_dwt2_8_neon(const uint8_t *src, const adm_dwt_band_t *dst, AdmBuffer *buf, int w, int h,
                     int src_stride, int dst_stride);

/* Scale-zero decouple, bit-identical to the scalar adm_decouple_cols() at every
 * enhancement gain limit (Netflix/vmaf 9e48141b). */
void adm_decouple_neon(AdmBuffer *buf, int w, int h, int stride, double adm_enhn_gain_limit,
                       int32_t *adm_div_lookup);

#endif /* ARM64_ADM_H_ */
