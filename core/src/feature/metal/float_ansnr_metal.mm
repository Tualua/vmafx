/**
 *  Copyright 2026 Lusoris and Claude (Anthropic)
 *  SPDX-License-Identifier: BSD-3-Clause-Plus-Patent
 *
 *  float_ansnr feature extractor on the Metal backend (T8-1f / ADR-0421).
 *  Dispatches `float_ansnr_kernel_{8,16}bpc` from float_ansnr.metal.
 *
 *  Two float partial buffers (sig_parts, noise_parts), one float per WG each.
 *  Scores:
 *    ansnr  = (noise==0) ? psnr_max : 10·log10(sig / noise)
 *    anpsnr = min(10·log10(peak² · W·H / max(noise, 1e-10)), psnr_max)
 *
 *  Peak / psnr_max table matches float_ansnr_vulkan.c::init().
 */

#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

extern "C" {
#include "dict.h"
#include "feature_collector.h"
#include "feature_extractor.h"
#include "feature_name.h"
#include "libvmaf/picture.h"

#include "../../metal/common.h"
#include "../../metal/kernel_template.h"
}

extern "C" {
extern const unsigned char libvmaf_metallib_start[] __asm("section$start$__TEXT$__metallib");
extern const unsigned char libvmaf_metallib_end[]   __asm("section$end$__TEXT$__metallib");
}

typedef struct FloatAnsnrStateMetal {
    VmafMetalKernelLifecycle lc;
    VmafMetalKernelBuffer rb_sig;    /* float sig_parts, grid_w × grid_h */
    VmafMetalKernelBuffer rb_noise;  /* float noise_parts, grid_w × grid_h */
    VmafMetalContext *ctx;
    void *pso_8bpc;
    void *pso_16bpc;

    double peak;
    double psnr_max;
    size_t plane_bytes;
    size_t partials_count;
    unsigned frame_w;
    unsigned frame_h;
    unsigned bpc;

    VmafDictionary *feature_name_dict;
} FloatAnsnrStateMetal;

static const VmafOption options[] = {{0}};

static int build_pipelines(FloatAnsnrStateMetal *s, id<MTLDevice> device)
{
    const size_t blob_size = (size_t)(libvmaf_metallib_end - libvmaf_metallib_start);
    if (blob_size == 0) { return -ENODEV; }

    dispatch_data_t data = dispatch_data_create(
        libvmaf_metallib_start, blob_size,
        dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0),
        DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    if (data == NULL) { return -ENOMEM; }

    NSError *err = nil;
    id<MTLLibrary> lib = [device newLibraryWithData:data error:&err];
    if (lib == nil) { return -ENODEV; }

    id<MTLFunction> fn8  = [lib newFunctionWithName:@"float_ansnr_kernel_8bpc"];
    id<MTLFunction> fn16 = [lib newFunctionWithName:@"float_ansnr_kernel_16bpc"];
    if (fn8 == nil || fn16 == nil) { return -ENODEV; }

    id<MTLComputePipelineState> pso8  = [device newComputePipelineStateWithFunction:fn8  error:&err];
    id<MTLComputePipelineState> pso16 = [device newComputePipelineStateWithFunction:fn16 error:&err];
    if (pso8 == nil || pso16 == nil) { return -ENODEV; }

    s->pso_8bpc  = (__bridge_retained void *)pso8;
    s->pso_16bpc = (__bridge_retained void *)pso16;
    return 0;
}

static int init_fex_metal(VmafFeatureExtractor *fex, enum VmafPixelFormat pix_fmt,
                          unsigned bpc, unsigned w, unsigned h)
{
    (void)pix_fmt;
    FloatAnsnrStateMetal *s = (FloatAnsnrStateMetal *)fex->priv;

    s->frame_w     = w;
    s->frame_h     = h;
    s->bpc         = bpc;
    s->plane_bytes = (size_t)w * h * (bpc <= 8u ? 1u : 2u);

    if (bpc == 8)       { s->peak = 255.0;         s->psnr_max = 60.0; }
    else if (bpc == 10) { s->peak = 255.75;        s->psnr_max = 72.0; }
    else if (bpc == 12) { s->peak = 255.9375;      s->psnr_max = 84.0; }
    else                { s->peak = 255.99609375;  s->psnr_max = 108.0; }

    int err = vmaf_metal_context_new(&s->ctx, 0);
    if (err != 0) { return err; }

    err = vmaf_metal_kernel_lifecycle_init(&s->lc, s->ctx);
    if (err != 0) { goto fail_ctx; }

    {
        const size_t grid_w   = (w + 15) / 16;
        const size_t grid_h   = (h + 15) / 16;
        s->partials_count     = grid_w * grid_h;
        const size_t par_size = s->partials_count * sizeof(float);
        err = vmaf_metal_kernel_buffer_alloc(&s->rb_sig,   s->ctx, par_size);
        if (err != 0) { goto fail_lc; }
        err = vmaf_metal_kernel_buffer_alloc(&s->rb_noise, s->ctx, par_size);
        if (err != 0) { goto fail_sig; }
    }

    {
        void *dh = vmaf_metal_context_device_handle(s->ctx);
        if (dh == NULL) { err = -ENODEV; goto fail_noise; }
        err = build_pipelines(s, (__bridge id<MTLDevice>)dh);
    }
    if (err != 0) { goto fail_noise; }

    s->feature_name_dict =
        vmaf_feature_name_dict_from_provided_features(fex->provided_features,
                                                      fex->options, s);
    if (s->feature_name_dict == NULL) { err = -ENOMEM; goto fail_pso; }
    return 0;

fail_pso:
    if (s->pso_8bpc)  { (void)(__bridge_transfer id<MTLComputePipelineState>)s->pso_8bpc;  s->pso_8bpc  = NULL; }
    if (s->pso_16bpc) { (void)(__bridge_transfer id<MTLComputePipelineState>)s->pso_16bpc; s->pso_16bpc = NULL; }
fail_noise:
    (void)vmaf_metal_kernel_buffer_free(&s->rb_noise, s->ctx);
fail_sig:
    (void)vmaf_metal_kernel_buffer_free(&s->rb_sig, s->ctx);
fail_lc:
    (void)vmaf_metal_kernel_lifecycle_close(&s->lc, s->ctx);
fail_ctx:
    vmaf_metal_context_destroy(s->ctx);
    s->ctx = NULL;
    return err;
}

static int submit_fex_metal(VmafFeatureExtractor *fex, VmafPicture *ref_pic,
                            VmafPicture *ref_pic_90, VmafPicture *dist_pic,
                            VmafPicture *dist_pic_90, unsigned index)
{
    (void)ref_pic_90; (void)dist_pic_90; (void)index;
    FloatAnsnrStateMetal *s = (FloatAnsnrStateMetal *)fex->priv;

    s->frame_w = ref_pic->w[0];
    s->frame_h = ref_pic->h[0];
    const size_t row_bytes = (size_t)s->frame_w * (s->bpc <= 8u ? 1u : 2u);

    void *dh = vmaf_metal_context_device_handle(s->ctx);
    void *qh = vmaf_metal_context_queue_handle(s->ctx);
    if (dh == NULL || qh == NULL) { return -ENODEV; }

    id<MTLDevice>      device   = (__bridge id<MTLDevice>)dh;
    id<MTLCommandQueue> queue   = (__bridge id<MTLCommandQueue>)qh;
    id<MTLBuffer>    sig_buf    = (__bridge id<MTLBuffer>)(void *)s->rb_sig.buffer;
    id<MTLBuffer>    noise_buf  = (__bridge id<MTLBuffer>)(void *)s->rb_noise.buffer;
    id<MTLComputePipelineState> pso = (s->bpc <= 8u)
        ? (__bridge id<MTLComputePipelineState>)s->pso_8bpc
        : (__bridge id<MTLComputePipelineState>)s->pso_16bpc;

    id<MTLBuffer> ref_buf = [device newBufferWithLength:s->plane_bytes options:MTLResourceStorageModeShared];
    id<MTLBuffer> dis_buf = [device newBufferWithLength:s->plane_bytes options:MTLResourceStorageModeShared];
    if (ref_buf == nil || dis_buf == nil) { return -ENOMEM; }
    {
        uint8_t *rd = (uint8_t *)[ref_buf contents];
        uint8_t *dd = (uint8_t *)[dis_buf contents];
        for (unsigned y = 0; y < s->frame_h; y++) {
            memcpy(rd + y * row_bytes, (uint8_t *)ref_pic->data[0] + y * ref_pic->stride[0], row_bytes);
            memcpy(dd + y * row_bytes, (uint8_t *)dist_pic->data[0] + y * dist_pic->stride[0], row_bytes);
        }
    }

    id<MTLCommandBuffer> cmd = [queue commandBuffer];
    if (cmd == nil) { return -ENOMEM; }

    const size_t par_size = s->partials_count * sizeof(float);
    id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
    [blit fillBuffer:sig_buf   range:NSMakeRange(0, par_size) value:0];
    [blit fillBuffer:noise_buf range:NSMakeRange(0, par_size) value:0];
    [blit endEncoding];

    id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
    [enc setComputePipelineState:pso];
    [enc setBuffer:ref_buf   offset:0 atIndex:0];
    [enc setBuffer:dis_buf   offset:0 atIndex:1];
    [enc setBuffer:sig_buf   offset:0 atIndex:2];
    [enc setBuffer:noise_buf offset:0 atIndex:3];
    if (s->bpc <= 8u) {
        uint32_t st[2] = {(uint32_t)row_bytes, (uint32_t)row_bytes};
        [enc setBytes:st length:sizeof(st) atIndex:4];
    } else {
        uint32_t st[4] = {(uint32_t)row_bytes, (uint32_t)row_bytes, (uint32_t)s->bpc, 0};
        [enc setBytes:st length:sizeof(st) atIndex:4];
    }
    uint32_t dim[2] = {(uint32_t)s->frame_w, (uint32_t)s->frame_h};
    [enc setBytes:dim length:sizeof(dim) atIndex:5];

    /* Threadgroup shared memory: two tiles 20×21 floats + vert tile */
    /* float_ansnr.metal uses 20×20 + TILE_PITCH=21 shared tiles */
    const size_t shm = (20u * 21u + 20u * 21u) * sizeof(float);
    [enc setThreadgroupMemoryLength:shm atIndex:0];

    MTLSize tg   = MTLSizeMake(16, 16, 1);
    MTLSize grid = MTLSizeMake((s->frame_w + 15) / 16, (s->frame_h + 15) / 16, 1);
    [enc dispatchThreadgroups:grid threadsPerThreadgroup:tg];
    [enc endEncoding];

    [cmd commit];
    [cmd waitUntilCompleted];
    return 0;
}

static int collect_fex_metal(VmafFeatureExtractor *fex, unsigned index,
                             VmafFeatureCollector *feature_collector)
{
    FloatAnsnrStateMetal *s = (FloatAnsnrStateMetal *)fex->priv;

    const float *sig_p   = (const float *)s->rb_sig.host_view;
    const float *noise_p = (const float *)s->rb_noise.host_view;
    double sig_sum = 0.0, noise_sum = 0.0;
    if (sig_p != NULL && noise_p != NULL) {
        for (size_t i = 0; i < s->partials_count; ++i) {
            sig_sum   += (double)sig_p[i];
            noise_sum += (double)noise_p[i];
        }
    }

    const double n_pix   = (double)s->frame_w * (double)s->frame_h;
    const double max_noise = (noise_sum < 1e-10) ? 1e-10 : noise_sum;
    const double score   = (noise_sum == 0.0) ? s->psnr_max
                                              : 10.0 * log10(sig_sum / noise_sum);
    double score_psnr = 10.0 * log10(s->peak * s->peak * n_pix / max_noise);
    if (score_psnr > s->psnr_max) { score_psnr = s->psnr_max; }

    int err = vmaf_feature_collector_append_with_dict(
        feature_collector, s->feature_name_dict, "float_ansnr", score, index);
    if (err != 0) { return err; }
    return vmaf_feature_collector_append_with_dict(
        feature_collector, s->feature_name_dict, "float_anpsnr", score_psnr, index);
}

static int close_fex_metal(VmafFeatureExtractor *fex)
{
    FloatAnsnrStateMetal *s = (FloatAnsnrStateMetal *)fex->priv;
    int rc = vmaf_metal_kernel_lifecycle_close(&s->lc, s->ctx);

    if (s->pso_16bpc) { (void)(__bridge_transfer id<MTLComputePipelineState>)s->pso_16bpc; s->pso_16bpc = NULL; }
    if (s->pso_8bpc)  { (void)(__bridge_transfer id<MTLComputePipelineState>)s->pso_8bpc;  s->pso_8bpc  = NULL; }

    int err = vmaf_metal_kernel_buffer_free(&s->rb_noise, s->ctx);
    if (err != 0 && rc == 0) { rc = err; }
    err = vmaf_metal_kernel_buffer_free(&s->rb_sig, s->ctx);
    if (err != 0 && rc == 0) { rc = err; }
    if (s->feature_name_dict) { (void)vmaf_dictionary_free(&s->feature_name_dict); }
    if (s->ctx) { vmaf_metal_context_destroy(s->ctx); s->ctx = NULL; }
    return rc;
}

static const char *provided_features[] = {"float_ansnr", "float_anpsnr", NULL};

extern "C" {
// NOLINTNEXTLINE(misc-use-internal-linkage)
VmafFeatureExtractor vmaf_fex_float_ansnr_metal = {
    .name              = "float_ansnr_metal",
    .init              = init_fex_metal,
    .submit            = submit_fex_metal,
    .collect           = collect_fex_metal,
    .flush             = NULL,
    .close             = close_fex_metal,
    .options           = options,
    .priv_size         = sizeof(FloatAnsnrStateMetal),
    .provided_features = provided_features,
    .flags             = 0,
    .chars = {
        .n_dispatches_per_frame = 1,
        .is_reduction_only      = true,
        .min_useful_frame_area  = 1920U * 1080U,
        .dispatch_hint          = VMAF_FEATURE_DISPATCH_AUTO,
    },
};
} /* extern "C" */
