/**
 *  Copyright 2016-2026 Netflix, Inc.
 *  Copyright (c) 2019 Joshua Holmer
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: BSD-2-Clause-Patent AND MIT
 *
 *  ciede2000 feature extractor on the CUDA backend (T7-23 /
 *  ADR-0182, GPU long-tail batch 1c part 2).
 *
 *  Single dispatch per frame. The kernel computes every pixel's CIEDE2000
 *  difference in the reference's arithmetic (integer_ciede/ciede_device.h,
 *  ADR-1426) and stores it at its raster position; the host reads the plane
 *  back, adds it in raster order into one double as ciede.c's extract() does,
 *  divides by w * h and applies `45 - 20 * log10(mean)`.
 */

#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "common.h"
#include "common/alignment.h"
#include "feature_collector.h"
#include "feature_extractor.h"
#include "feature_name.h"
#include "cuda/integer_ciede/ciede_device.h"
#include "cuda/integer_ciede_cuda.h"
#include "cuda/kernel_template.h"
#include "mem.h"
#include "picture.h"
#include "picture_cuda.h"
#include "cuda_helper.cuh"

/* NOLINTBEGIN(modernize-use-nullptr): C translation unit. The fork builds C as
 * C23, where clang-tidy also proposes the `nullptr` keyword, but this is a C
 * translation unit whose sources spell the null pointer constant `NULL` and
 * MSVC's documented /std:clatest C23 feature set does not include `nullptr`
 * while the required Windows build compiles this TU with cl.exe. ADR-1138. */

typedef struct CiedeStateCuda {
    /* Stream + event pair owned by `cuda/kernel_template.h` lifecycle
     * (ADR-0246). */
    VmafCudaKernelLifecycle lc;
    /* One float per pixel, in raster order: device + pinned host. Owned by
     * the template's readback bundle. */
    VmafCudaKernelReadback rb;

    CUfunction funcbpc8;
    CUfunction funcbpc16;
    /* PTX module backing the CIEDE kernels — owned here so
     * `close_fex_cuda` can unload it. Skipping the unload leaks
     * ~200-500 KB of GPU-resident PTX backing store per vmaf_close(). */
    CUmodule module;
    size_t term_capacity;
    unsigned index;
    unsigned frame_w;
    unsigned frame_h;
    unsigned bpc;
    unsigned ss_hor;
    unsigned ss_ver;
    VmafDictionary *feature_name_dict;
} CiedeStateCuda;

static const VmafOption options[] = {{0}};

static int ciede_cuda_dispatch(const VmafPicture *ref, const VmafPicture *dis,
                               VmafCudaBuffer *terms, unsigned width, unsigned height, unsigned bpc,
                               unsigned ss_hor, unsigned ss_ver, CUfunction funcbpc8,
                               CUfunction funcbpc16, CudaFunctions *cu_f, CUstream stream)
{
    const int block_dim_x = CIEDE_BLOCK_X;
    const int block_dim_y = CIEDE_BLOCK_Y;
    const int grid_dim_x = DIV_ROUND_UP(width, block_dim_x);
    const int grid_dim_y = DIV_ROUND_UP(height, block_dim_y);

    void *kernelParams[] = {(void *)ref, (void *)dis, (void *)terms, &width,
                            &height,     &bpc,        &ss_hor,       &ss_ver};
    CUfunction func = (bpc == 8) ? funcbpc8 : funcbpc16;
    CHECK_CUDA_RETURN(cu_f, cuLaunchKernel(func, grid_dim_x, grid_dim_y, 1, block_dim_x,
                                           block_dim_y, 1, 0, stream, kernelParams, NULL));
    return 0;
}

/* ------------------------------------------------------------------ */
/* ciede_init_unwind - the single teardown path for init_fex_cuda.
 *
 * Drain the lifecycle before releasing anything queued work may reference.
 * The original init failure remains the first returned error.
 */
static int ciede_init_unwind(VmafFeatureExtractor *fex, CiedeStateCuda *s, int err)
{
    const int lifecycle_rc = vmaf_cuda_kernel_lifecycle_close(&s->lc, fex->cu_state);
    if (lifecycle_rc)
        return err ? err : lifecycle_rc;

    int rc = err;
    const int rb_rc = vmaf_cuda_kernel_readback_free(&s->rb, fex->cu_state);
    if (!rc)
        rc = rb_rc;
    const int dict_rc = vmaf_dictionary_free(&s->feature_name_dict);
    if (!rc)
        rc = dict_rc;
    const int module_rc = vmaf_cuda_module_unload(fex->cu_state, &s->module);
    if (!rc)
        rc = module_rc;
    return rc;
}

static int init_fex_cuda(VmafFeatureExtractor *fex, enum VmafPixelFormat pix_fmt, unsigned bpc,
                         unsigned w, unsigned h)
{
    if (pix_fmt == VMAF_PIX_FMT_YUV400P)
        return -EINVAL;
    CiedeStateCuda *s = fex->priv;
    CudaFunctions *cu_f = fex->cu_state->f;

    /* Stream + event pair via the template — handles ctx push/pop +
     * rollback on failure. */
    int err = vmaf_cuda_kernel_lifecycle_init(&s->lc, fex->cu_state);
    if (err)
        return ciede_init_unwind(fex, s, err);

    /* Module + function lookup is metric-specific; the template
     * doesn't (yet) own that step. */
    int _cuda_err = 0;
    int ctx_pushed = 0;
    CHECK_CUDA_GOTO(cu_f, cuCtxPushCurrent(fex->cu_state->ctx), fail);
    ctx_pushed = 1;

    CHECK_CUDA_GOTO(cu_f, cuModuleLoadData(&s->module, ciede_score_ptx), fail);
    CHECK_CUDA_GOTO(
        cu_f, cuModuleGetFunction(&s->funcbpc8, s->module, "calculate_ciede_kernel_8bpc"), fail);
    CHECK_CUDA_GOTO(
        cu_f, cuModuleGetFunction(&s->funcbpc16, s->module, "calculate_ciede_kernel_16bpc"), fail);

    CHECK_CUDA_GOTO(cu_f, cuCtxPopCurrent(NULL), fail);

    s->bpc = bpc;
    s->ss_hor = (pix_fmt != VMAF_PIX_FMT_YUV444P) ? 1u : 0u;
    s->ss_ver = (pix_fmt == VMAF_PIX_FMT_YUV420P) ? 1u : 0u;

    /* One term per pixel of the announced (w, h). */
    s->term_capacity = (size_t)w * h;

    err = vmaf_cuda_kernel_readback_alloc(&s->rb, fex->cu_state, s->term_capacity * sizeof(float));
    if (err)
        return ciede_init_unwind(fex, s, err);

    s->feature_name_dict =
        vmaf_feature_name_dict_from_provided_features(fex->provided_features, fex->options, s);
    if (!s->feature_name_dict) {
        err = -ENOMEM;
        return ciede_init_unwind(fex, s, err);
    }

    return 0;

fail:
    if (ctx_pushed)
        (void)cu_f->cuCtxPopCurrent(NULL);
    return ciede_init_unwind(fex, s, _cuda_err);
}

static int submit_fex_cuda(VmafFeatureExtractor *fex, VmafPicture *ref_pic, VmafPicture *ref_pic_90,
                           VmafPicture *dist_pic, VmafPicture *dist_pic_90, unsigned index)
{
    (void)ref_pic_90;
    (void)dist_pic_90;
    CiedeStateCuda *s = fex->priv;
    CudaFunctions *cu_f = fex->cu_state->f;

    s->index = index;
    s->frame_w = ref_pic->w[0];
    s->frame_h = ref_pic->h[0];
    const size_t term_count = (size_t)s->frame_w * s->frame_h;
    if (term_count > s->term_capacity)
        return -EINVAL;

    /* Intentionally inline the pre-launch wait rather than calling
     * vmaf_cuda_kernel_submit_pre_launch — ciede's kernel writes every
     * term of the plane (no atomic), so the template's memset is
     * unnecessary. The lifecycle / readback / collect helpers still
     * apply. */
    CHECK_CUDA_RETURN(cu_f, cuStreamWaitEvent(vmaf_cuda_picture_get_stream(ref_pic),
                                              vmaf_cuda_picture_get_ready_event(dist_pic),
                                              CU_EVENT_WAIT_DEFAULT));

    int err = ciede_cuda_dispatch(ref_pic, dist_pic, s->rb.device, ref_pic->w[0], ref_pic->h[0],
                                  s->bpc, s->ss_hor, s->ss_ver, s->funcbpc8, s->funcbpc16, cu_f,
                                  vmaf_cuda_picture_get_stream(ref_pic));
    if (err)
        return err;

    CHECK_CUDA_RETURN(cu_f, cuEventRecord(s->lc.submit, vmaf_cuda_picture_get_stream(ref_pic)));
    CHECK_CUDA_RETURN(cu_f, cuStreamWaitEvent(s->lc.str, s->lc.submit, CU_EVENT_WAIT_DEFAULT));

    CHECK_CUDA_RETURN(cu_f, cuMemcpyDtoHAsync(s->rb.host_pinned, (CUdeviceptr)s->rb.device->data,
                                              term_count * sizeof(float), s->lc.str));
    return vmaf_cuda_kernel_submit_post_record(&s->lc, fex->cu_state);
}

static int collect_fex_cuda(VmafFeatureExtractor *fex, unsigned index,
                            VmafFeatureCollector *feature_collector)
{
    CiedeStateCuda *s = fex->priv;

    int err = vmaf_cuda_kernel_collect_wait(&s->lc, fex->cu_state);
    if (err)
        return err;

    /* ciede.c's extract(): every pixel's value into one double in raster
     * order, then `45. - 20. * log10(de00_sum / (w * h))`. */
    const double de00_sum =
        ciede_frame_sum((const float *)s->rb.host_pinned, (size_t)s->frame_w * s->frame_h);
    const double score = 45. - 20. * log10(de00_sum / (s->frame_w * s->frame_h));

    return vmaf_feature_collector_append_with_dict(feature_collector, s->feature_name_dict,
                                                   "ciede2000", score, index);
}

static int close_fex_cuda(VmafFeatureExtractor *fex)
{
    CiedeStateCuda *s = fex->priv;
    int rc = vmaf_cuda_kernel_lifecycle_close(&s->lc, fex->cu_state);
    if (rc)
        return rc;

    const int rb_rc = vmaf_cuda_kernel_readback_free(&s->rb, fex->cu_state);
    if (!rc)
        rc = rb_rc;
    const int dict_rc = vmaf_dictionary_free(&s->feature_name_dict);
    if (!rc)
        rc = dict_rc;
    const int module_rc = vmaf_cuda_module_unload(fex->cu_state, &s->module);
    if (!rc)
        rc = module_rc;
    return rc;
}

static const char *provided_features[] = {"ciede2000", NULL};

// NOLINTNEXTLINE(misc-use-internal-linkage): cross-TU registry pattern — external linkage required; referenced as `extern VmafFeatureExtractor vmaf_fex_ciede_cuda` by feature_extractor.cpp's feature_extractor_list[] (ADR-0278).
VmafFeatureExtractor vmaf_fex_ciede_cuda = {
    .name = "ciede_cuda",
    .init = init_fex_cuda,
    .submit = submit_fex_cuda,
    .collect = collect_fex_cuda,
    .close = close_fex_cuda,
    .options = options,
    .priv_size = sizeof(CiedeStateCuda),
    .provided_features = provided_features,
    .flags = VMAF_FEATURE_EXTRACTOR_CUDA,
    .chars =
        {
            .n_dispatches_per_frame = 1,
            .is_reduction_only = false,
            .min_useful_frame_area = 1920U * 1080U,
            .dispatch_hint = VMAF_FEATURE_DISPATCH_AUTO,
        },
};

/* NOLINTEND(modernize-use-nullptr) */
