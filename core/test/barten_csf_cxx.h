/**
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: EUPL-1.2
 */

#ifndef VMAF_TEST_BARTEN_CSF_CXX_H_
#define VMAF_TEST_BARTEN_CSF_CXX_H_

#ifdef __cplusplus
extern "C" {
#endif

/* barten_csf() of feature/barten_csf_tools.h, compiled as C++ (ADR-1489). */
float vmaf_test_barten_csf_cxx(int lambda, double view_dist, int display_height, double lum_level,
                               double csf_scale);

#ifdef __cplusplus
}
#endif

#endif /* VMAF_TEST_BARTEN_CSF_CXX_H_ */
