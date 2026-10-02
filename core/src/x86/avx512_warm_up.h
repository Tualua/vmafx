/**
 *
 *  Copyright 2026 Lusoris
 *
 *  SPDX-License-Identifier: EUPL-1.2
 */

#ifndef VMAF_SRC_X86_AVX512_WARM_UP_H_
#define VMAF_SRC_X86_AVX512_WARM_UP_H_

/* Run one 512-bit instruction. On Intel processors the 512-bit execution units
 * power down when idle and take 10 to 20 microseconds to come back;
 * vmaf_init_cpu() calls this on a host with AVX-512 so the first frame does
 * not pay for it. Call it only where VMAF_X86_CPU_FLAG_AVX512 is set.
 *
 * GNU inline assembly: a no-op under MSVC, which has none on x64.
 *
 * The clobber list names xmm0 as well as zmm0. clang drops the clobber of a
 * register the enclosing function's target does not have, and no caller of
 * this function is compiled for AVX-512: with "zmm0" alone, a value the
 * caller keeps in xmm0 across the statement is zeroed. A link-time-optimised
 * clang build inlined vmaf_init_cpu() into its caller that way.
 *
 * C header that a C++ translation unit (cpu.cpp) includes too; `(void)` is
 * the spelling both languages accept on every required toolchain. */
// NOLINTNEXTLINE(modernize-redundant-void-arg): C header read under a C++ translation unit; `(void)` is the C spelling. ADR-1138.
static inline void vmaf_x86_avx512_warm_up(void)
{
#if defined(__GNUC__) || defined(__clang__)
    __asm__ volatile("vpxord %%zmm0, %%zmm0, %%zmm0" ::: "xmm0", "zmm0");
#endif
}

#endif /* VMAF_SRC_X86_AVX512_WARM_UP_H_ */
