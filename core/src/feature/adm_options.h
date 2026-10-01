/**
 *
 *  Copyright 2016-2020 Netflix, Inc.
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

#pragma once

#ifndef ADM_OPTIONS_H_
#define ADM_OPTIONS_H_

/* Percentage of frame to discard on all 4 sides */
#define ADM_BORDER_FACTOR (0.1)

/* Whether to use a trigonometry-free method for comparing angles. */
#define ADM_OPT_AVOID_ATAN

/* Whether to save intermediate results to files. */
/* #define ADM_OPT_DEBUG_DUMP */

/* Upstream defines ADM_OPT_RECIP_DIVISION here: division by multiplying with a
 * reciprocal refined from the processor's RCPSS estimate. That estimate is
 * specified by an error bound, not bit for bit, so float ADM's scores depended
 * on the processor. The fork divides (ADR-1442): the decouple's quotient is
 * the IEEE fp32 one on every host and compiler. Do not define the macro;
 * adm_tools.c refuses to compile with it. */

/* Enhancement gain imposed on adm, must be >= 1.0, where 1.0 means the gain is completely disabled */
#define DEFAULT_ADM_ENHN_GAIN_LIMIT (100.0)

/* normalized viewing distance = viewing distance / ref display's physical height */
#define DEFAULT_ADM_NORM_VIEW_DIST (3.0)

/* reference display height in pixels */
#define DEFAULT_ADM_REF_DISPLAY_HEIGHT (1080)

/* noise multiplicative weight */
#define DEFAULT_ADM_NOISE_WEIGHT (0.03125)

/* CSF scale factor */
#define DEFAULT_ADM_CSF_SCALE (1.0)

/* CSF diagonal scale factor */
#define DEFAULT_ADM_CSF_DIAG_SCALE (1.0)

/* Contrast sensitivity function */
enum ADM_CSF_MODE {
    ADM_CSF_MODE_WATSON97 = 0,
    ADM_CSF_MODE_BARTEN,
    ADM_CSF_MODE_ADM,
};

/* Default contrast sensitivity function */
#define DEFAULT_ADM_CSF_MODE (ADM_CSF_MODE_WATSON97)

/* Default luminance level (in cd/m2) for contrast sensitivity function calculation */
#define DEFAULT_ADM_CSF_LUMINANCE_LEVEL (100.0)

/* Default minimum value allowed for the feature */
#define DEFAULT_ADM_MIN_VAL (0.0)

#endif /* ADM_OPTIONS_H_ */
