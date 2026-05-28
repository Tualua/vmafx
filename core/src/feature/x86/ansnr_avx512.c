/**
 *
 *  Copyright 2016-2026 Netflix, Inc.
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

#include <immintrin.h>
#include "ansnr_avx512.h"

void ansnr_mse_line_avx512(const float *ref, const float *dis, float *sig_accum, float *noise_accum,
                           int w)
{
    double sig_result = 0.0;
    double noise_result = 0.0;
    int j = 0;

    for (; j + 16 <= w; j += 16) {
        __m512 r = _mm512_loadu_ps(ref + j);
        __m512 d = _mm512_loadu_ps(dis + j);
        __m512 diff = _mm512_sub_ps(r, d);
        __m512 sig_val = _mm512_mul_ps(r, r);
        __m512 noise_val = _mm512_mul_ps(diff, diff);

        _Alignas(64) float stmp[16];
        _Alignas(64) float ntmp[16];
        _mm512_store_ps(stmp, sig_val);
        _mm512_store_ps(ntmp, noise_val);
        for (int k = 0; k < 16; k++) {
            sig_result += (double)stmp[k];
            noise_result += (double)ntmp[k];
        }
    }

    for (; j < w; j++) {
        float r = ref[j];
        float d = dis[j];
        float diff = r - d;
        sig_result += (double)r * r;
        noise_result += (double)diff * diff;
    }

    *sig_accum += (float)sig_result;
    *noise_accum += (float)noise_result;
}
