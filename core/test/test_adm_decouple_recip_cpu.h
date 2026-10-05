/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

#ifndef TEST_ADM_DECOUPLE_RECIP_CPU_H_
#define TEST_ADM_DECOUPLE_RECIP_CPU_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t adm_recip_cpu_table(int o);
int adm_recip_cpu_sample(int o, int t, int angle_flag, double gain);

#ifdef __cplusplus
}
#endif

#endif /* TEST_ADM_DECOUPLE_RECIP_CPU_H_ */
