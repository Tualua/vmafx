/**
 *  Copyright 2026 Lusoris and Claude (Anthropic)
 *  SPDX-License-Identifier: BSD-3-Clause-Plus-Patent
 *
 *  float_ansnr feature extractor on the HIP backend — fifth consumer
 *  of `libvmaf/src/hip/kernel_template.h` (T7-10b follow-up /
 *  ADR-0266).  Real kernel promotion: T7-10b batch-1 / ADR-0372.
 *
 *  This TU mirrors `libvmaf/src/feature/cuda/float_ansnr_cuda.c`
 *  call-graph-for-call-graph. When `HAVE_HIPCC` is defined (i.e.,
 *  `enable_hipcc=true` at configure time), the `init`, `submit`, and
 *  `collect` functions use real HIP Module API calls following the
 *  canonical pattern established by PR #612 / ADR-0254.
 *
 *  Without `HAVE_HIPCC` (CPU-only builds), the scaffold posture is
 *  preserved: every lifecycle helper returns -ENOSYS.
 *
 *  Algorithm (mirrors CPU `ansnr.c::compute_ansnr`):
 *    1. ref/dis pixels normalised to float in [-128, ~128].
 *    2. 3x3 ref Gaussian filter -> ref_filtr.
 *    3. 5x5 dis filter (weights /571) -> filtd.
 *    4. sig   += ref_filtr^2
 *       noise += (ref_filtr - filtd)^2
 *  Per-block partial pair: partials[2*block_idx+0]=sig, [+1]=noise.
 *  Host accumulates in double and emits:
 *    float_ansnr  = 10*log10(sig/noise)                    (or psnr_max)
 *    float_anpsnr = MIN(10*log10(peak^2*w*h/max(noise,1e-10)), psnr_max)
 *
 *  Like the CUDA twin, the submit path intentionally bypasses
 *  `vmaf_hip_kernel_submit_pre_launch` because the kernel writes
 *  per-block partials directly (no atomic accumulator, no memset
 *  prerequisite). This bypass is the load-bearing ADR-0372 invariant.
 */

#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "dict.h"
#include "feature_collector.h"
#include "feature_extractor.h"
#include "feature_name.h"
#include "libvmaf/picture.h"

#include "../../hip/common.h"
#include "../../hip/kernel_template.h"
#include "float_ansnr_hip.h"

#ifdef HAVE_HIPCC
#define __HIP_PLATFORM_AMD__ 1
#include <hip/hip_runtime_api.h>

/* HSACO fat binary embedded by xxd -i during the meson hipcc pipeline
 * (ADR-0372 / `hip_hsaco_sources` meson block). Defined by the
 * auto-generated `float_ansnr_score_hsaco.c` custom_target output. */
extern const unsigned char float_ansnr_score_hsaco[];
extern const unsigned int float_ansnr_score_hsaco_len;
#endif /* HAVE_HIPCC */

typedef struct FloatAnsnrStateHip {
    /* Lifecycle (private stream + submit/finished event pair) and the
     * (device per-block (sig, noise) float partials, pinned host
     * readback slot) pair are managed by `hip/kernel_template.h`
     * (T7-10b fifth consumer / ADR-0266). The struct shape mirrors
     * the CUDA twin's `FloatAnsnrStateCuda`. */
    VmafHipKernelLifecycle lc;
    VmafHipKernelReadback rb;
    VmafHipContext *ctx;
    unsigned wg_count;
    unsigned index;
    unsigned frame_w;
    unsigned frame_h;
    unsigned bpc;
    double peak;
    double psnr_max;
#ifdef HAVE_HIPCC
    hipModule_t module;
    hipFunction_t funcbpc8;
    hipFunction_t funcbpc16;
    /* Per-frame staging buffers for ref + dis luma planes. */
    void *ref_in;
    void *dis_in;
#endif /* HAVE_HIPCC */
    VmafDictionary *feature_name_dict;
} FloatAnsnrStateHip;

/* Mirrors the CUDA twin's 16x16 workgroup tile (ANSNR_BX / ANSNR_BY). */
#define ANSNR_HIP_BX 16
#define ANSNR_HIP_BY 16

static const VmafOption options[] = {{0}};

#ifdef HAVE_HIPCC
/* Translate a HIP error code to a negative errno. Mirrors
 * `hip_rc_to_errno` in `kernel_template.c`. */
static int ansnr_hip_rc(hipError_t rc)
{
    if (rc == hipSuccess)
        return 0;
    switch (rc) {
    case hipErrorInvalidValue:
    case hipErrorInvalidHandle:
        return -EINVAL;
    case hipErrorOutOfMemory:
        return -ENOMEM;
    case hipErrorNoDevice:
    case hipErrorInvalidDevice:
        return -ENODEV;
    case hipErrorNotSupported:
        return -ENOSYS;
    default:
        return -EIO;
    }
}

/* Load the HSACO module and look up the two kernel entry points.
 * Called once from init() when HAVE_HIPCC is defined. */
static int ansnr_hip_module_load(FloatAnsnrStateHip *s)
{
    hipError_t rc = hipModuleLoadData(&s->module, float_ansnr_score_hsaco);
    if (rc != hipSuccess)
        return ansnr_hip_rc(rc);

    rc = hipModuleGetFunction(&s->funcbpc8, s->module, "float_ansnr_kernel_8bpc");
    if (rc != hipSuccess) {
        (void)hipModuleUnload(s->module);
        s->module = NULL;
        return ansnr_hip_rc(rc);
    }
    rc = hipModuleGetFunction(&s->funcbpc16, s->module, "float_ansnr_kernel_16bpc");
    if (rc != hipSuccess) {
        (void)hipModuleUnload(s->module);
        s->module = NULL;
        return ansnr_hip_rc(rc);
    }
    return 0;
}

/* Launch the appropriate bpc kernel on `pic_stream`. The kernel writes
 * per-block (sig, noise) float partials to `rb.device` directly — no
 * atomic accumulator, so no prior memset is needed (CUDA twin uses the
 * same bypass pattern for float_ansnr). After launch, record submit,
 * wait on private stream, DtoH copy partials, record finished. */
static int ansnr_hip_launch(FloatAnsnrStateHip *s, uintptr_t pic_stream)
{
    hipStream_t str = (hipStream_t)s->lc.str;
    hipStream_t pstr = (hipStream_t)pic_stream;

    const ptrdiff_t plane_pitch = (ptrdiff_t)(s->frame_w * (s->bpc <= 8u ? 1u : 2u));
    const unsigned gx = (s->frame_w + ANSNR_HIP_BX - 1u) / ANSNR_HIP_BX;
    const unsigned gy = (s->frame_h + ANSNR_HIP_BY - 1u) / ANSNR_HIP_BY;

    float *partials_dev = (float *)s->rb.device;
    const uint8_t *ref_dev = (const uint8_t *)s->ref_in;
    const uint8_t *dis_dev = (const uint8_t *)s->dis_in;
    unsigned w = s->frame_w;
    unsigned h = s->frame_h;
    unsigned bpc = s->bpc;

    hipError_t rc;
    if (s->bpc == 8u) {
        /* float_ansnr_kernel_8bpc(ref, dis, ref_stride, dis_stride,
         *                         partials, width, height) */
        void *args[] = {
            (void *)&ref_dev,      (void *)&dis_dev, (void *)&plane_pitch, (void *)&plane_pitch,
            (void *)&partials_dev, (void *)&w,       (void *)&h,
        };
        rc = hipModuleLaunchKernel(s->funcbpc8, gx, gy, 1, ANSNR_HIP_BX, ANSNR_HIP_BY, 1, 0, pstr,
                                   args, NULL);
    } else {
        /* float_ansnr_kernel_16bpc(ref, dis, ref_stride, dis_stride,
         *                          partials, width, height, bpc) */
        void *args[] = {
            (void *)&ref_dev,      (void *)&dis_dev, (void *)&plane_pitch, (void *)&plane_pitch,
            (void *)&partials_dev, (void *)&w,       (void *)&h,           (void *)&bpc,
        };
        rc = hipModuleLaunchKernel(s->funcbpc16, gx, gy, 1, ANSNR_HIP_BX, ANSNR_HIP_BY, 1, 0, pstr,
                                   args, NULL);
    }
    if (rc != hipSuccess)
        return ansnr_hip_rc(rc);

    rc = hipEventRecord((hipEvent_t)s->lc.submit, pstr);
    if (rc != hipSuccess)
        return ansnr_hip_rc(rc);
    rc = hipStreamWaitEvent(str, (hipEvent_t)s->lc.submit, 0);
    if (rc != hipSuccess)
        return ansnr_hip_rc(rc);

    rc = hipMemcpyAsync(s->rb.host_pinned, s->rb.device, (size_t)s->wg_count * 2u * sizeof(float),
                        hipMemcpyDeviceToHost, str);
    if (rc != hipSuccess)
        return ansnr_hip_rc(rc);

    return vmaf_hip_kernel_submit_post_record(&s->lc, s->ctx);
}
#endif /* HAVE_HIPCC */

static int init_fex_hip(VmafFeatureExtractor *fex, enum VmafPixelFormat pix_fmt, unsigned bpc,
                        unsigned w, unsigned h)
{
    (void)pix_fmt;
    FloatAnsnrStateHip *s = fex->priv;

    s->frame_w = w;
    s->frame_h = h;
    s->bpc = bpc;

    /* peak / psnr_max table mirrors the CUDA twin verbatim. */
    if (bpc == 8u) {
        s->peak = 255.0;
        s->psnr_max = 60.0;
    } else if (bpc == 10u) {
        s->peak = 255.75;
        s->psnr_max = 72.0;
    } else if (bpc == 12u) {
        s->peak = 255.9375;
        s->psnr_max = 84.0;
    } else if (bpc == 16u) {
        s->peak = 255.99609375;
        s->psnr_max = 108.0;
    } else {
        return -EINVAL;
    }

    int err = vmaf_hip_context_new(&s->ctx, 0);
    if (err != 0)
        return err;

    err = vmaf_hip_kernel_lifecycle_init(&s->lc, s->ctx);
    if (err != 0)
        goto fail_after_ctx;

    const unsigned grid_x = (w + (ANSNR_HIP_BX - 1u)) / ANSNR_HIP_BX;
    const unsigned grid_y = (h + (ANSNR_HIP_BY - 1u)) / ANSNR_HIP_BY;
    s->wg_count = grid_x * grid_y;

    /* Readback pair: device interleaved (sig, noise) float partials +
     * pinned host slot, sized at 2 floats per workgroup. */
    err = vmaf_hip_kernel_readback_alloc(&s->rb, s->ctx, (size_t)s->wg_count * 2u * sizeof(float));
    if (err != 0)
        goto fail_after_lc;

#ifdef HAVE_HIPCC
    err = ansnr_hip_module_load(s);
    if (err != 0)
        goto fail_after_rb;

    /* Staging buffers for ref + dis luma planes. */
    const size_t bpp = (bpc <= 8u) ? 1u : 2u;
    const size_t plane_bytes = (size_t)w * h * bpp;
    hipError_t rc = hipMalloc(&s->ref_in, plane_bytes);
    if (rc != hipSuccess) {
        err = -ENOMEM;
        goto fail_after_module;
    }
    rc = hipMalloc(&s->dis_in, plane_bytes);
    if (rc != hipSuccess) {
        (void)hipFree(s->ref_in);
        s->ref_in = NULL;
        err = -ENOMEM;
        goto fail_after_module;
    }
#endif /* HAVE_HIPCC */

    s->feature_name_dict =
        vmaf_feature_name_dict_from_provided_features(fex->provided_features, fex->options, s);
    if (s->feature_name_dict == NULL) {
        err = -ENOMEM;
#ifdef HAVE_HIPCC
        goto fail_after_bufs;
#else
        goto fail_after_rb;
#endif
    }

    return 0;

#ifdef HAVE_HIPCC
fail_after_bufs:
    if (s->dis_in != NULL) {
        (void)hipFree(s->dis_in);
        s->dis_in = NULL;
    }
    if (s->ref_in != NULL) {
        (void)hipFree(s->ref_in);
        s->ref_in = NULL;
    }
fail_after_module:
    if (s->module != NULL) {
        (void)hipModuleUnload(s->module);
        s->module = NULL;
    }
#endif /* HAVE_HIPCC */
fail_after_rb:
    (void)vmaf_hip_kernel_readback_free(&s->rb, s->ctx);
fail_after_lc:
    (void)vmaf_hip_kernel_lifecycle_close(&s->lc, s->ctx);
fail_after_ctx:
    vmaf_hip_context_destroy(s->ctx);
    s->ctx = NULL;
    return err;
}

static int submit_fex_hip(VmafFeatureExtractor *fex, VmafPicture *ref_pic, VmafPicture *ref_pic_90,
                          VmafPicture *dist_pic, VmafPicture *dist_pic_90, unsigned index)
{
    (void)ref_pic_90;
    (void)dist_pic_90;
    FloatAnsnrStateHip *s = fex->priv;

    s->index = index;
    s->frame_w = ref_pic->w[0];
    s->frame_h = ref_pic->h[0];

#ifdef HAVE_HIPCC
    const ptrdiff_t plane_pitch = (ptrdiff_t)(s->frame_w * (s->bpc <= 8u ? 1u : 2u));
    const uintptr_t pic_stream_handle = 0;

    hipError_t rc =
        hipMemcpy2DAsync(s->ref_in, (size_t)plane_pitch, ref_pic->data[0],
                         (size_t)ref_pic->stride[0], (size_t)plane_pitch, (size_t)s->frame_h,
                         hipMemcpyHostToDevice, (hipStream_t)pic_stream_handle);
    if (rc != hipSuccess)
        return -EIO;

    rc = hipMemcpy2DAsync(s->dis_in, (size_t)plane_pitch, dist_pic->data[0],
                          (size_t)dist_pic->stride[0], (size_t)plane_pitch, (size_t)s->frame_h,
                          hipMemcpyHostToDevice, (hipStream_t)pic_stream_handle);
    if (rc != hipSuccess)
        return -EIO;

    /* The ansnr kernel writes per-block partials directly — no prior
     * memset / zero of the accumulator needed. The CUDA twin makes the
     * same choice and documents it as the intentional bypass. */
    return ansnr_hip_launch(s, pic_stream_handle);
#else
    /* Scaffold posture (no HAVE_HIPCC). */
    (void)dist_pic;
    return -ENOSYS;
#endif /* HAVE_HIPCC */
}

static int collect_fex_hip(VmafFeatureExtractor *fex, unsigned index,
                           VmafFeatureCollector *feature_collector)
{
    FloatAnsnrStateHip *s = fex->priv;

    int err = vmaf_hip_kernel_collect_wait(&s->lc, s->ctx);
    if (err != 0)
        return err;

#ifdef HAVE_HIPCC
    /* Accumulate interleaved (sig, noise) float partials in double.
     * Matches the CUDA twin's cross-block reduction precision posture. */
    const float *partials_host = (const float *)s->rb.host_pinned;
    double sig = 0.0;
    double noise = 0.0;
    for (unsigned i = 0; i < s->wg_count; i++) {
        sig += (double)partials_host[2u * i + 0u];
        noise += (double)partials_host[2u * i + 1u];
    }

    /* float_ansnr formula matches CPU ansnr.c and the CUDA twin. */
    const double score = (noise == 0.0) ? s->psnr_max : 10.0 * log10(sig / noise);
    const double eps = 1e-10;
    const double n_pix = (double)s->frame_w * (double)s->frame_h;
    const double max_noise = (noise > eps) ? noise : eps;
    double score_psnr = 10.0 * log10(s->peak * s->peak * n_pix / max_noise);
    if (score_psnr > s->psnr_max)
        score_psnr = s->psnr_max;

    err = vmaf_feature_collector_append_with_dict(feature_collector, s->feature_name_dict,
                                                  "float_ansnr", score, index);
    if (err != 0)
        return err;
    return vmaf_feature_collector_append_with_dict(feature_collector, s->feature_name_dict,
                                                   "float_anpsnr", score_psnr, index);
#else
    (void)feature_collector;
    (void)index;
    return -ENOSYS;
#endif /* HAVE_HIPCC */
}

static int close_fex_hip(VmafFeatureExtractor *fex)
{
    FloatAnsnrStateHip *s = fex->priv;

    int rc = vmaf_hip_kernel_lifecycle_close(&s->lc, s->ctx);
    int err = vmaf_hip_kernel_readback_free(&s->rb, s->ctx);
    if (err != 0 && rc == 0)
        rc = err;

#ifdef HAVE_HIPCC
    if (s->dis_in != NULL) {
        (void)hipFree(s->dis_in);
        s->dis_in = NULL;
    }
    if (s->ref_in != NULL) {
        (void)hipFree(s->ref_in);
        s->ref_in = NULL;
    }
    if (s->module != NULL) {
        hipError_t hip_err = hipModuleUnload(s->module);
        if (hip_err != hipSuccess && rc == 0)
            rc = -EIO;
        s->module = NULL;
    }
#endif /* HAVE_HIPCC */

    if (s->feature_name_dict != NULL) {
        err = vmaf_dictionary_free(&s->feature_name_dict);
        if (err != 0 && rc == 0)
            rc = err;
    }
    if (s->ctx != NULL) {
        vmaf_hip_context_destroy(s->ctx);
        s->ctx = NULL;
    }
    return rc;
}

static const char *provided_features[] = {"float_ansnr", "float_anpsnr", NULL};

/* Load-bearing: registered via `extern VmafFeatureExtractor vmaf_fex_float_ansnr_hip;`
 * in `libvmaf/src/feature/feature_extractor.c`'s `feature_extractor_list[]`.
 * Same pattern as every CUDA / SYCL / Vulkan feature extractor. */
// NOLINTNEXTLINE(misc-use-internal-linkage)
VmafFeatureExtractor vmaf_fex_float_ansnr_hip = {
    .name = "float_ansnr_hip",
    .init = init_fex_hip,
    .submit = submit_fex_hip,
    .collect = collect_fex_hip,
    .close = close_fex_hip,
    .options = options,
    .priv_size = sizeof(FloatAnsnrStateHip),
    .provided_features = provided_features,
    /* Intentionally no VMAF_FEATURE_EXTRACTOR_HIP flag yet — the
     * picture buffer-type plumbing for HIP lands with the runtime
     * PR (T7-10b). Until then the consumer registers as a
     * "CPU-flagged" extractor whose `init()` returns -ENOSYS on
     * non-ROCm builds. Same posture as the first through fourth
     * consumers (ADR-0241 / ADR-0254 / ADR-0259 / ADR-0260). */
    .flags = 0,
    .chars =
        {
            .n_dispatches_per_frame = 1,
            .is_reduction_only = false,
            .min_useful_frame_area = 1920U * 1080U,
            .dispatch_hint = VMAF_FEATURE_DISPATCH_AUTO,
        },
};
