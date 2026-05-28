/**
 *  Copyright 2026 Lusoris and Claude (Anthropic)
 *  SPDX-License-Identifier: BSD-3-Clause-Plus-Patent
 *
 *  float_ansnr feature kernel on the SYCL backend (T7-23 / batch 3
 *  part 2c — ADR-0192 / ADR-0194). SYCL twin of float_ansnr_vulkan
 *  + float_ansnr_cuda.
 *
 *  Single-dispatch SYCL kernel applies the 3x3 ref filter and 5x5
 *  dis filter from libvmaf/src/feature/ansnr_tools.c, accumulates
 *  per-pixel sig (ref_filtr²) and noise ((ref_filtr - filtd)²)
 *  contributions, and emits per-WG (sig, noise) float pairs. Host
 *  reduces in `double` and applies the CPU formulas:
 *
 *    float_ansnr  = 10 * log10(sig / noise)   (or psnr_max if noise == 0)
 *    float_anpsnr = MIN(10*log10(peak² · w · h / max(noise, 1e-10)), psnr_max)
 *
 *  Self-contained submit / collect — does NOT register with
 *  vmaf_sycl_graph_register because ansnr needs the dis plane (the
 *  shared_frame buffer is luma-only and reused across feature
 *  contexts, but the upload pattern here is simple enough that the
 *  graph integration adds no value).
 *
 *  Mirror padding: edge-replicating (`2*size - idx - 1`) — same as
 *  motion_v2_sycl + the Vulkan / CUDA twins of this kernel.
 *
 *  Precision contract per ADR-0192: places=3. Empirically lands at
 *  places=4+ on the cross-backend gate fixture.
 */

#include <sycl/sycl.hpp>

#include "sycl_compat.h"

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>

extern "C" {
#include "config.h"
#include "feature_collector.h"
#include "feature_extractor.h"
#include "feature_name.h"
#include "log.h"
#include "picture.h"
#include "sycl/common.h"
}

namespace
{

struct AnsnrStateSycl {
    unsigned width;
    unsigned height;
    unsigned bpc;
    double peak;
    double psnr_max;
    size_t plane_bytes;

    VmafSyclState *sycl_state;

    /* Pinned host staging for ref + dis raw pixels. */
    void *h_ref;
    void *h_dis;

    /* Device USM for ref + dis raw pixels. */
    void *d_ref;
    void *d_dis;

    /* Per-WG (sig, noise) float pairs. */
    float *d_partials;
    float *h_partials;
    unsigned wg_count_x;
    unsigned wg_count_y;
    unsigned wg_count;

    bool has_pending;
    unsigned pending_index;

    VmafDictionary *feature_name_dict;
};

static constexpr int ANSNR_WG_X = 16;
static constexpr int ANSNR_WG_Y = 16;
static constexpr int ANSNR_HALF = 2;
static constexpr int ANSNR_TILE_W = ANSNR_WG_X + 2 * ANSNR_HALF; /* 20 */
static constexpr int ANSNR_TILE_H = ANSNR_WG_Y + 2 * ANSNR_HALF; /* 20 */

static constexpr float ANSNR_FILT_REF[9] = {
    1.0f / 16.0f, 2.0f / 16.0f, 1.0f / 16.0f, 2.0f / 16.0f, 4.0f / 16.0f,
    2.0f / 16.0f, 1.0f / 16.0f, 2.0f / 16.0f, 1.0f / 16.0f,
};

static constexpr float ANSNR_FILT_DIS[25] = {
    2.0f / 571.0f,  7.0f / 571.0f,  12.0f / 571.0f,  7.0f / 571.0f,  2.0f / 571.0f,
    7.0f / 571.0f,  31.0f / 571.0f, 52.0f / 571.0f,  31.0f / 571.0f, 7.0f / 571.0f,
    12.0f / 571.0f, 52.0f / 571.0f, 127.0f / 571.0f, 52.0f / 571.0f, 12.0f / 571.0f,
    7.0f / 571.0f,  31.0f / 571.0f, 52.0f / 571.0f,  31.0f / 571.0f, 7.0f / 571.0f,
    2.0f / 571.0f,  7.0f / 571.0f,  12.0f / 571.0f,  7.0f / 571.0f,  2.0f / 571.0f,
};

static inline int dev_mirror_ansnr(int idx, int sup)
{
    if (idx < 0)
        return -idx;
    if (idx >= sup)
        return 2 * sup - idx - 1;
    return idx;
}

static sycl::event launch_ansnr(sycl::queue &q, const void *ref, const void *dis, float *partials,
                                unsigned width, unsigned height, unsigned bpc, unsigned wg_count_x)
{
    const size_t global_x = ((width + ANSNR_WG_X - 1) / ANSNR_WG_X) * ANSNR_WG_X;
    const size_t global_y = ((height + ANSNR_WG_Y - 1) / ANSNR_WG_Y) * ANSNR_WG_Y;
    const unsigned e_w = width;
    const unsigned e_h = height;
    const unsigned e_bpc = bpc;
    const unsigned e_wgx = wg_count_x;
    const void *e_ref = ref;
    const void *e_dis = dis;

    return q.submit([&](sycl::handler &cgh) {
        sycl::local_accessor<float, 2> s_ref(sycl::range<2>(ANSNR_TILE_H, ANSNR_TILE_W), cgh);
        sycl::local_accessor<float, 2> s_dis(sycl::range<2>(ANSNR_TILE_H, ANSNR_TILE_W), cgh);
        constexpr int MAX_SUBGROUPS = ANSNR_WG_X * ANSNR_WG_Y;
        sycl::local_accessor<float, 1> s_sig_warps(sycl::range<1>(MAX_SUBGROUPS), cgh);
        sycl::local_accessor<float, 1> s_noise_warps(sycl::range<1>(MAX_SUBGROUPS), cgh);

        cgh.parallel_for(
            sycl::nd_range<2>(sycl::range<2>(global_y, global_x),
                              sycl::range<2>(ANSNR_WG_Y, ANSNR_WG_X)),
            [=](sycl::nd_item<2> item) VMAF_SYCL_REQD_SG_SIZE(32) {
                const int gx = (int)item.get_global_id(1);
                const int gy = (int)item.get_global_id(0);
                const unsigned lid = item.get_local_linear_id();
                const unsigned lx = item.get_local_id(1);
                const unsigned ly = item.get_local_id(0);
                const bool valid = (gx < (int)e_w && gy < (int)e_h);

                float scaler = 1.0f;
                if (e_bpc == 10)
                    scaler = 4.0f;
                else if (e_bpc == 12)
                    scaler = 16.0f;
                else if (e_bpc == 16)
                    scaler = 256.0f;
                const float inv_scaler = 1.0f / scaler;

                /* --- Phase 1: cooperative tile load with mirror padding --- */
                const int tile_oy = (int)(item.get_group(0) * ANSNR_WG_Y) - ANSNR_HALF;
                const int tile_ox = (int)(item.get_group(1) * ANSNR_WG_X) - ANSNR_HALF;
                constexpr unsigned tile_elems = ANSNR_TILE_H * ANSNR_TILE_W;
                constexpr unsigned WG_SIZE = ANSNR_WG_X * ANSNR_WG_Y;

                auto read_pix = [&](const void *plane, int y, int x) -> float {
                    if (e_bpc <= 8) {
                        const uint8_t v =
                            static_cast<const uint8_t *>(plane)[(size_t)y * e_w + (size_t)x];
                        return (float)v - 128.0f;
                    }
                    const uint16_t v =
                        static_cast<const uint16_t *>(plane)[(size_t)y * e_w + (size_t)x];
                    return (float)v * inv_scaler - 128.0f;
                };

                const bool interior = (tile_oy >= 0) && (tile_oy + ANSNR_TILE_H <= (int)e_h) &&
                                      (tile_ox >= 0) && (tile_ox + ANSNR_TILE_W <= (int)e_w);

                for (unsigned i = lid; i < tile_elems; i += WG_SIZE) {
                    const unsigned tr = i / ANSNR_TILE_W;
                    const unsigned tc = i % ANSNR_TILE_W;
                    int py = tile_oy + (int)tr;
                    int px = tile_ox + (int)tc;
                    if (!interior) {
                        py = dev_mirror_ansnr(py, (int)e_h);
                        px = dev_mirror_ansnr(px, (int)e_w);
                    }
                    s_ref[tr][tc] = read_pix(e_ref, py, px);
                    s_dis[tr][tc] = read_pix(e_dis, py, px);
                }
                item.barrier(sycl::access::fence_space::local_space);

                /* --- Phase 2: per-thread 3x3 ref + 5x5 dis filters --- */
                float my_sig = 0.0f;
                float my_noise = 0.0f;
                if (valid) {
                    float ref_filtr = 0.0f;
                    for (int k = 0; k < 3; k++) {
                        for (int l = 0; l < 3; l++) {
                            ref_filtr += ANSNR_FILT_REF[k * 3 + l] * s_ref[ly + 1 + k][lx + 1 + l];
                        }
                    }
                    float filtd = 0.0f;
                    for (int k = 0; k < 5; k++) {
                        for (int l = 0; l < 5; l++) {
                            filtd += ANSNR_FILT_DIS[k * 5 + l] * s_dis[ly + k][lx + l];
                        }
                    }
                    my_sig = ref_filtr * ref_filtr;
                    const float diff = ref_filtr - filtd;
                    my_noise = diff * diff;
                }

                /* --- Phase 3: subgroup + cross-subgroup reductions --- */
                sycl::sub_group sg = item.get_sub_group();
                const float sg_sig = sycl::reduce_over_group(sg, my_sig, sycl::plus<float>{});
                const float sg_noise = sycl::reduce_over_group(sg, my_noise, sycl::plus<float>{});
                const uint32_t sg_id = sg.get_group_linear_id();
                const uint32_t sg_lid = sg.get_local_linear_id();
                const uint32_t n_subgroups = sg.get_group_linear_range();
                if (sg_lid == 0) {
                    s_sig_warps[sg_id] = sg_sig;
                    s_noise_warps[sg_id] = sg_noise;
                }
                item.barrier(sycl::access::fence_space::local_space);

                if (lid == 0) {
                    float total_sig = 0.0f;
                    float total_noise = 0.0f;
                    for (uint32_t s = 0; s < n_subgroups; s++) {
                        total_sig += s_sig_warps[s];
                        total_noise += s_noise_warps[s];
                    }
                    const size_t wg_idx = item.get_group(0) * e_wgx + item.get_group(1);
                    partials[2 * wg_idx + 0] = total_sig;
                    partials[2 * wg_idx + 1] = total_noise;
                }
            });
    });
}

template <typename T>
static void copy_y_plane(const VmafPicture *pic, void *dst, unsigned w, unsigned h)
{
    const T *src = static_cast<const T *>(pic->data[0]);
    T *out = static_cast<T *>(dst);
    const ptrdiff_t src_stride_t = pic->stride[0] / static_cast<ptrdiff_t>(sizeof(T));
    for (unsigned i = 0; i < h; i++) {
        for (unsigned j = 0; j < w; j++)
            out[j] = src[j];
        src += src_stride_t;
        out += w;
    }
}

} /* anonymous namespace */

extern "C" {

static const VmafOption options_ansnr_sycl[] = {{0}};

static int init_fex_sycl(VmafFeatureExtractor *fex, enum VmafPixelFormat pix_fmt, unsigned bpc,
                         unsigned w, unsigned h)
{
    (void)pix_fmt;
    auto *s = static_cast<AnsnrStateSycl *>(fex->priv);
    s->width = w;
    s->height = h;
    s->bpc = bpc;
    s->has_pending = false;

    if (bpc == 8) {
        s->peak = 255.0;
        s->psnr_max = 60.0;
    } else if (bpc == 10) {
        s->peak = 255.75;
        s->psnr_max = 72.0;
    } else if (bpc == 12) {
        s->peak = 255.9375;
        s->psnr_max = 84.0;
    } else if (bpc == 16) {
        s->peak = 255.99609375;
        s->psnr_max = 108.0;
    } else {
        return -EINVAL;
    }

    if (!fex->sycl_state) {
        vmaf_log(VMAF_LOG_LEVEL_ERROR, "float_ansnr_sycl: no SYCL state\n");
        return -EINVAL;
    }
    VmafSyclState *state = fex->sycl_state;
    s->sycl_state = state;

    s->plane_bytes = (size_t)w * h * (bpc <= 8 ? 1u : 2u);
    s->h_ref = vmaf_sycl_malloc_host(state, s->plane_bytes);
    s->h_dis = vmaf_sycl_malloc_host(state, s->plane_bytes);
    s->d_ref = vmaf_sycl_malloc_device(state, s->plane_bytes);
    s->d_dis = vmaf_sycl_malloc_device(state, s->plane_bytes);

    s->wg_count_x = (unsigned)((w + ANSNR_WG_X - 1) / ANSNR_WG_X);
    s->wg_count_y = (unsigned)((h + ANSNR_WG_Y - 1) / ANSNR_WG_Y);
    s->wg_count = s->wg_count_x * s->wg_count_y;
    const size_t partial_bytes = (size_t)s->wg_count * 2u * sizeof(float);
    s->d_partials = static_cast<float *>(vmaf_sycl_malloc_device(state, partial_bytes));
    s->h_partials = static_cast<float *>(vmaf_sycl_malloc_host(state, partial_bytes));

    if (!s->h_ref || !s->h_dis || !s->d_ref || !s->d_dis || !s->d_partials || !s->h_partials) {
        vmaf_log(VMAF_LOG_LEVEL_ERROR, "float_ansnr_sycl: USM allocation failed\n");
        return -ENOMEM;
    }

    s->feature_name_dict =
        vmaf_feature_name_dict_from_provided_features(fex->provided_features, fex->options, s);
    if (!s->feature_name_dict)
        return -ENOMEM;

    return 0;
}

static int submit_fex_sycl(VmafFeatureExtractor *fex, VmafPicture *ref_pic, VmafPicture *ref_pic_90,
                           VmafPicture *dist_pic, VmafPicture *dist_pic_90, unsigned index)
{
    (void)ref_pic_90;
    (void)dist_pic_90;
    auto *s = static_cast<AnsnrStateSycl *>(fex->priv);
    auto *qptr = static_cast<sycl::queue *>(vmaf_sycl_get_queue_ptr(s->sycl_state));
    if (!qptr)
        return -EINVAL;
    sycl::queue &q = *qptr;

    if (s->bpc <= 8) {
        copy_y_plane<uint8_t>(ref_pic, s->h_ref, s->width, s->height);
        copy_y_plane<uint8_t>(dist_pic, s->h_dis, s->width, s->height);
    } else {
        copy_y_plane<uint16_t>(ref_pic, s->h_ref, s->width, s->height);
        copy_y_plane<uint16_t>(dist_pic, s->h_dis, s->width, s->height);
    }
    q.memcpy(s->d_ref, s->h_ref, s->plane_bytes);
    q.memcpy(s->d_dis, s->h_dis, s->plane_bytes);

    launch_ansnr(q, s->d_ref, s->d_dis, s->d_partials, s->width, s->height, s->bpc, s->wg_count_x);

    q.memcpy(s->h_partials, s->d_partials, (size_t)s->wg_count * 2u * sizeof(float));

    s->pending_index = index;
    s->has_pending = true;
    return 0;
}

static int collect_fex_sycl(VmafFeatureExtractor *fex, unsigned index,
                            VmafFeatureCollector *feature_collector)
{
    auto *s = static_cast<AnsnrStateSycl *>(fex->priv);
    auto *qptr = static_cast<sycl::queue *>(vmaf_sycl_get_queue_ptr(s->sycl_state));
    if (!qptr)
        return -EINVAL;
    qptr->wait();

    double sig = 0.0;
    double noise = 0.0;
    for (unsigned i = 0; i < s->wg_count; i++) {
        sig += (double)s->h_partials[2 * i + 0];
        noise += (double)s->h_partials[2 * i + 1];
    }

    const double score = (noise == 0.0) ? s->psnr_max : 10.0 * std::log10(sig / noise);
    const double eps = 1e-10;
    const double n_pix = (double)s->width * (double)s->height;
    const double max_noise = noise > eps ? noise : eps;
    double score_psnr = 10.0 * std::log10(s->peak * s->peak * n_pix / max_noise);
    if (score_psnr > s->psnr_max)
        score_psnr = s->psnr_max;

    int err = vmaf_feature_collector_append_with_dict(feature_collector, s->feature_name_dict,
                                                      "float_ansnr", score, index);
    if (err)
        return err;
    return vmaf_feature_collector_append_with_dict(feature_collector, s->feature_name_dict,
                                                   "float_anpsnr", score_psnr, index);
}

static int close_fex_sycl(VmafFeatureExtractor *fex)
{
    auto *s = static_cast<AnsnrStateSycl *>(fex->priv);
    if (s->sycl_state) {
        if (s->h_ref)
            vmaf_sycl_free(s->sycl_state, s->h_ref);
        if (s->h_dis)
            vmaf_sycl_free(s->sycl_state, s->h_dis);
        if (s->d_ref)
            vmaf_sycl_free(s->sycl_state, s->d_ref);
        if (s->d_dis)
            vmaf_sycl_free(s->sycl_state, s->d_dis);
        if (s->d_partials)
            vmaf_sycl_free(s->sycl_state, s->d_partials);
        if (s->h_partials)
            vmaf_sycl_free(s->sycl_state, s->h_partials);
    }
    if (s->feature_name_dict)
        vmaf_dictionary_free(&s->feature_name_dict);
    return 0;
}

static const char *provided_features_ansnr_sycl[] = {"float_ansnr", "float_anpsnr", NULL};

extern "C" VmafFeatureExtractor vmaf_fex_float_ansnr_sycl = {
    .name = "float_ansnr_sycl",
    .init = init_fex_sycl,
    .extract = NULL,
    .flush = NULL,
    .close = close_fex_sycl,
    .submit = submit_fex_sycl,
    .collect = collect_fex_sycl,
    .options = options_ansnr_sycl,
    .priv_size = sizeof(AnsnrStateSycl),
    .flags = VMAF_FEATURE_EXTRACTOR_SYCL,
    .provided_features = provided_features_ansnr_sycl,
};

} /* extern "C" */
