/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * Fixed-point ssim CPU vs. SYCL parity (first added as a places=4 test,
 * ADR-0884; equality since ADR-1443).
 *
 * SSIM is integer_ssim.c on the CPU (registered as "ssim") and
 * integer_ssim_sycl.cpp::vmaf_fex_integer_ssim_sycl on SYCL; both emit the
 * feature "ssim". The window moments are int64 on both sides. The reference
 * forms each pixel's term in fp64 and adds every term into one double in
 * raster order. Since ADR-1443 the kernel runs the reference's fp64
 * operations on values held in 64-bit integers
 * (feature/sycl/sycl_integer_ssim_math.h; a SYCL kernel has no fp64 type,
 * ADR-0220), stores the bit pattern of every pixel's term, and the host adds
 * the plane in the reference's order. The score is the CPU's bit for bit, so
 * this test asserts equality where it asserted 1e-4.
 *
 * Before ADR-1443 the kernel formed the term in fp32 and added fp32 partial
 * sums per 16x8 block; it was 7e-9 to 3e-7 from the CPU on video. Twelve of
 * the fifteen cases below fail on it: nine differ by 8e-10 to 6e-8 (6.5e-7 in
 * dB), the one-pixel frame included, and the three 16-bit cases stop with
 * `invalid ratio` because the fp32 denominator overflows. The three
 * identical-frame cases pass on both.
 *
 * The fixtures, the comparison and the cases are ssim_twin_parity.h's.
 *
 * Skip behaviour: exits 77 when there is no SYCL device.
 */

#include "libvmaf/libvmaf_sycl.h"

#include "ssim_twin_parity.h"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy proposes `nullptr`, but the required MSVC C build does
 * not provide that keyword. Preserve the portable C spelling. ADR-1138. */

static int twin_open(void **state)
{
    VmafSyclState *sycl_state = NULL;
    VmafSyclConfiguration sycl_cfg = {.device_index = -1};
    const int err = vmaf_sycl_state_init(&sycl_state, sycl_cfg);
    *state = sycl_state;
    return err;
}

static int twin_import(VmafContext *vmaf, void *state)
{
    return vmaf_sycl_import_state(vmaf, (VmafSyclState *)state);
}

static int twin_close(void *state)
{
    VmafSyclState *sycl_state = (VmafSyclState *)state;
    vmaf_sycl_state_free(&sycl_state);
    return 0;
}

static const SsimTwin twin = {
    .extractor = "integer_ssim_sycl",
    .backend = "SYCL",
    .open = twin_open,
    .import = twin_import,
    .close = twin_close,
};

static char *test_ssim_sycl_registered(void)
{
    return ssim_twin_registered(&twin);
}

static char *test_ssim_8bit(void)
{
    return ssim_twin_bit_depth(&twin, 8u);
}

static char *test_ssim_10bit(void)
{
    return ssim_twin_bit_depth(&twin, 10u);
}

static char *test_ssim_12bit(void)
{
    return ssim_twin_bit_depth(&twin, 12u);
}

static char *test_ssim_16bit(void)
{
    return ssim_twin_bit_depth(&twin, 16u);
}

static char *test_ssim_odd_frame(void)
{
    return ssim_twin_odd_frame(&twin);
}

static char *test_ssim_tiny_frame(void)
{
    return ssim_twin_tiny_frame(&twin, 8u);
}

static char *test_ssim_tiny_frame_16bit(void)
{
    return ssim_twin_tiny_frame(&twin, 16u);
}

static char *test_ssim_one_pixel(void)
{
    return ssim_twin_one_pixel(&twin);
}

static char *test_ssim_1080p(void)
{
    return ssim_twin_1080p(&twin);
}

static char *test_ssim_enable_db(void)
{
    return ssim_twin_enable_db(&twin);
}

static char *test_ssim_inverted(void)
{
    return ssim_twin_inverted(&twin, 8u);
}

static char *test_ssim_inverted_16bit(void)
{
    return ssim_twin_inverted(&twin, 16u);
}

static char *test_ssim_identical(void)
{
    return ssim_twin_identical(&twin, 323u, 181u, NULL);
}

static char *test_ssim_identical_clipped(void)
{
    return ssim_twin_identical(&twin, 323u, 181u, "clip_db");
}

static char *test_ssim_identical_tiny(void)
{
    return ssim_twin_identical(&twin, 3u, 3u, NULL);
}

static char *run_bit_depth_cases(void)
{
    mu_run_test(test_ssim_8bit);
    mu_run_test(test_ssim_10bit);
    mu_run_test(test_ssim_12bit);
    mu_run_test(test_ssim_16bit);
    return NULL;
}

static char *run_geometry_cases(void)
{
    mu_run_test(test_ssim_odd_frame);
    mu_run_test(test_ssim_tiny_frame);
    mu_run_test(test_ssim_tiny_frame_16bit);
    mu_run_test(test_ssim_one_pixel);
    mu_run_test(test_ssim_1080p);
    return NULL;
}

static char *run_content_and_option_cases(void)
{
    mu_run_test(test_ssim_enable_db);
    mu_run_test(test_ssim_inverted);
    mu_run_test(test_ssim_inverted_16bit);
    mu_run_test(test_ssim_identical);
    mu_run_test(test_ssim_identical_clipped);
    mu_run_test(test_ssim_identical_tiny);
    return NULL;
}

char *run_tests(void)
{
    mu_run_test(test_ssim_sycl_registered);
    mu_assert_msg(run_bit_depth_cases());
    mu_assert_msg(run_geometry_cases());
    mu_assert_msg(run_content_and_option_cases());
    return NULL;
}

/* NOLINTEND(modernize-use-nullptr) */
