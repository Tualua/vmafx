/**
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: EUPL-1.2
 */

/*
 * barten_csf() as a C++ translation unit evaluates it (ADR-1489).
 *
 * feature/barten_csf_tools.h is compiled as C by the CPU extractors and as
 * C++ by the SYCL and Metal twins of integer ADM. In C++ `pow(float, float)`,
 * `exp(float)` and `log10(float)` are the float functions, in C the double
 * ones, so an expression that leaves the promotion to the language returns
 * one value in C and another in C++. test_float_adm_csf_upstream.c compares
 * this function with the C one bit for bit.
 */

#include "barten_csf_cxx.h"

#include "feature/barten_csf_tools.h"

float vmaf_test_barten_csf_cxx(int lambda, double view_dist, int display_height, double lum_level,
                               double csf_scale)
{
    return barten_csf(lambda, view_dist, display_height, lum_level, csf_scale);
}
