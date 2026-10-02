/* Upstream-mirror filename: defines float_moment symbol despite the integer_ prefix (matches Netflix upstream). See ADR-0549. */
/**
 *  Copyright 2016-2026 Netflix, Inc.
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 *  float_moment feature extractor on the SYCL backend
 *  (T7-23 / ADR-0182, GPU long-tail batch 1d part 3). SYCL twin
 *  of moment_vulkan (PR #133) and moment_cuda (this PR's batch
 *  1d part 2).
 *
 *  Algorithm (mirrors core/src/feature/float_moment.c::extract):
 *      for each pixel:
 *          ref1 += ref;        ref2 += ref * ref;
 *          dis1 += dis;        dis2 += dis * dis;
 *      host divides each accumulator by w*h.
 *
 *  Single kernel per frame; four sycl::atomic_ref<int64_t>
 *  reductions into the four-slot device counter. Pattern:
 *  register with vmaf_sycl_graph_register and ride the combined
 *  graph submit/wait machinery (mirrors psnr_sycl).
 *
 *  Bit-exactness contract (ADR-1449): the four sums are the CPU's.
 *  moment.c adds the samples, and their squares, into one double per
 *  output. The first moments' terms are exact, so their integer sum is the
 *  CPU's sum. For the second moments the CPU forms each square in float
 *  (`const float term = pic_ * pic_`) and adds the floats: up to 12 bits a
 *  square has at most 24 significant bits and the float is the integer
 *  square, but at 16 bits it is the square rounded to 24 bits. The kernel
 *  therefore adds the float square (moment_float_square()), an integer
 *  below 2^32, so the int64 sum is exact and equals the CPU's double sum
 *  while that sum is below 2^53 units of 1 / scaler^2, which holds for
 *  every frame of up to 2^21 pixels and for every 8-, 10- and 12-bit
 *  frame. Beyond it the CPU's own running sum rounds at every add; see
 *  collect_fex_sycl().
 */

#include <sycl/sycl.hpp>

#include <cerrno>
#include <cstdint>
#include <cstdlib>

#include "config.h"
#include "feature_collector.h"
#include "feature_extractor.h"
#include "feature_name.h"
#include "log.h"
#include "picture.h"
#include "sycl/common.h"
#include "sycl_compat.h"

namespace
{

struct MomentStateSycl {
    /* Frame geometry. */
    unsigned width;
    unsigned height;
    unsigned bpc;

    /* SYCL state back-pointer. */
    VmafSyclState *sycl_state;

    /* Device + host accumulators — 4 slots: ref1, dis1, ref2, dis2. */
    int64_t *d_sums;
    int64_t *h_sums;

    /* Submit/collect plumbing. */
    bool has_pending;
    unsigned pending_index;

    VmafDictionary *feature_name_dict;
};

/* The term of moment.c::compute_2nd_moment() for the raw sample `v`, in
 * units of 1 / scaler^2: picture_copy() divides the sample by the scaler (a
 * power of two, exact) and the square is one fp32 product, rounded to nearest
 * even. Scaling by a power of two does not change which bits are rounded
 * away, so the float square of the raw sample has the same significand. Its
 * value is an integer below 2^32. Up to 12 bits it is the integer square. */
VMAF_SYCL_ALWAYS_INLINE int64_t moment_float_square(uint32_t v)
{
    const auto sample = (float)v;
    const float square = sample * sample;
    return (int64_t)square;
}

/* Per-pixel moment kernel. Reads the shared ref/dis frame
 * buffers (uint8 packed at ≤8bpc, uint16 packed at ≥10bpc,
 * tightly packed at `width * bytes_per_pixel`). Atomic-adds
 * each pixel's four contributions to the device accumulators. */
static void launch_moment(sycl::queue &q, void *shared_ref, void *shared_dis, int64_t *d_sums,
                          unsigned width, unsigned height, unsigned bpc)
{
    sycl::range<2> const global{(size_t)height, (size_t)width};
    const unsigned e_w = width;
    const unsigned e_bpc = bpc;
    void const *ref_in = shared_ref;
    void const *dis_in = shared_dis;
    int64_t *const e_sums = d_sums;

    q.submit([=](sycl::handler &h) {
        h.parallel_for(global, [=](sycl::id<2> id) {
            const size_t y = id[0];
            const size_t x = id[1];
            const size_t off = y * (size_t)e_w + x;
            uint32_t r;
            uint32_t d;
            if (e_bpc <= 8) {
                r = static_cast<const uint8_t *>(ref_in)[off];
                d = static_cast<const uint8_t *>(dis_in)[off];
            } else {
                r = static_cast<const uint16_t *>(ref_in)[off];
                d = static_cast<const uint16_t *>(dis_in)[off];
            }
            using atomic64 =
                sycl::atomic_ref<int64_t, sycl::memory_order::relaxed, sycl::memory_scope::device,
                                 sycl::access::address_space::global_space>;
            atomic64(e_sums[0]).fetch_add((int64_t)r);
            atomic64(e_sums[1]).fetch_add((int64_t)d);
            atomic64(e_sums[2]).fetch_add(moment_float_square(r));
            atomic64(e_sums[3]).fetch_add(moment_float_square(d));
        });
    });
}

/* Pre-graph: zero all four accumulators. */
static void moment_pre_graph(void *queue_ptr, void *priv)
{
    sycl::queue &q = *static_cast<sycl::queue *>(queue_ptr);
    auto *s = static_cast<MomentStateSycl *>(priv);
    q.memset(s->d_sums, 0, 4u * sizeof(int64_t));
}

static void enqueue_moment_work(void *queue_ptr, void *priv, void *shared_ref, void *shared_dis)
{
    sycl::queue &q = *static_cast<sycl::queue *>(queue_ptr);
    auto *s = static_cast<MomentStateSycl *>(priv);
    launch_moment(q, shared_ref, shared_dis, s->d_sums, s->width, s->height, s->bpc);
}

static void moment_post_graph(void *queue_ptr, void *priv)
{
    sycl::queue &q = *static_cast<sycl::queue *>(queue_ptr);
    auto *s = static_cast<MomentStateSycl *>(priv);
    q.memcpy(s->h_sums, s->d_sums, 4u * sizeof(int64_t));
}

static void config_moment_slot(void *priv, int slot)
{
    (void)priv;
    (void)slot;
}

} /* anonymous namespace */

extern "C" {

// NOLINTBEGIN(misc-use-anonymous-namespace, misc-use-internal-linkage) — ADR-0141 §2 load-bearing invariant: the
// `init_fex_sycl` / `submit_fex_sycl` / `collect_fex_sycl` / `close_fex_sycl`
// entry points and the `provided_features_*` table use C-style `static` rather
// than an anonymous namespace because their addresses are stored in the
// `extern "C" VmafFeatureExtractor` struct at the bottom of this file, which
// the C ABI consumes through the function-pointer types in
// `feature_extractor.h`. A namespace cannot appear inside this linkage
// specification at all. Same band, same reason, as integer_motion_sycl.cpp and
// integer_adm_sycl.cpp. Per CLAUDE.md §12 r12 these are load-bearing
// invariants of the SYCL <-> libvmaf C-API ABI.
static const VmafOption options_moment_sycl[] = {{.name = nullptr}};

static int close_fex_sycl(VmafFeatureExtractor *fex);

static int init_fex_sycl(VmafFeatureExtractor *fex, enum VmafPixelFormat pix_fmt, unsigned bpc,
                         unsigned w, unsigned h)
{
    (void)pix_fmt;
    auto *s = static_cast<MomentStateSycl *>(fex->priv);

    s->width = w;
    s->height = h;
    s->bpc = bpc;

    if (!fex->sycl_state) {
        vmaf_log(VMAF_LOG_LEVEL_ERROR, "float_moment_sycl: no SYCL state\n");
        return -EINVAL;
    }

    VmafSyclState *state = fex->sycl_state;
    s->sycl_state = state;

    int const err = vmaf_sycl_shared_frame_init(state, w, h, bpc);
    if (err)
        return err;

    s->d_sums = static_cast<int64_t *>(vmaf_sycl_malloc_device(state, 4u * sizeof(int64_t)));
    s->h_sums = static_cast<int64_t *>(vmaf_sycl_malloc_host(state, 4u * sizeof(int64_t)));
    if (!s->d_sums || !s->h_sums) {
        vmaf_log(VMAF_LOG_LEVEL_ERROR, "float_moment_sycl: device memory allocation failed\n");
        (void)close_fex_sycl(fex);
        return -ENOMEM;
    }

    s->feature_name_dict =
        vmaf_feature_name_dict_from_provided_features(fex->provided_features, fex->options, s);
    if (!s->feature_name_dict) {
        (void)close_fex_sycl(fex);
        return -ENOMEM;
    }

    s->has_pending = false;

    int const err2 = vmaf_sycl_graph_register(state, enqueue_moment_work, moment_pre_graph,
                                              moment_post_graph, config_moment_slot, s, "MOMENT");
    if (err2) {
        (void)close_fex_sycl(fex);
        return err2;
    }

    return 0;
}

static int submit_fex_sycl(VmafFeatureExtractor *fex, VmafPicture *ref_pic, VmafPicture *ref_pic_90,
                           VmafPicture *dist_pic, VmafPicture *dist_pic_90, unsigned index)
{
    (void)ref_pic;
    (void)ref_pic_90;
    (void)dist_pic;
    (void)dist_pic_90;

    auto *s = static_cast<MomentStateSycl *>(fex->priv);
    VmafSyclState *state = fex->sycl_state;

    int const err = vmaf_sycl_graph_submit(state);
    if (err)
        return err;

    s->pending_index = index;
    s->has_pending = true;
    return 0;
}

static int collect_fex_sycl(VmafFeatureExtractor *fex, unsigned index,
                            VmafFeatureCollector *feature_collector)
{
    auto *s = static_cast<MomentStateSycl *>(fex->priv);
    VmafSyclState *state = fex->sycl_state;

    /* A failed wait leaves the sums stale: fail, never score them. */
    int const wait_err = vmaf_sycl_graph_wait(state);
    if (wait_err)
        return wait_err;

    /* ADR-1212: normalise by the bit-depth scaler exactly as the CPU reference
     * does. `float_moment` runs `picture_copy()` first, which divides every
     * sample by 4 (10 bpc), 16 (12 bpc) or 256 (16 bpc) before `moment.c`
     * accumulates it — see core/src/feature/picture_copy.cpp. This kernel
     * accumulates the RAW codeword, so without this step a 10-bit input
     * reported ref1st/dis1st 4x and ref2nd/dis2nd 16x too large. The device
     * sums are exact integers, so dividing here reproduces the CPU's
     * sum(x / scaler) bit for bit: every term is an exact multiple of
     * 1 / scaler (first moments) or 1 / scaler^2 (the float squares the
     * kernel adds, ADR-1449), and the CPU's running double sum is exact while
     * it is below 2^53 of those units. A term is below 2^32 units, so that
     * covers every frame of up to 2^21 pixels and every 8-, 10- and 12-bit
     * frame. On a larger 16-bit frame whose sum of squares passes 2^53 units
     * the CPU rounds each further add and this exact sum, rounded once, can
     * differ from it by at most
     * (pixels - 2^21 + 1) / pixels * 2^(e - 69) + 2^-37, e the binade of the
     * sum in units (T-HIP-FLOAT-MOMENT-PAST-2-53-2026-10-02). */
    const double moment_scaler = (s->bpc == 10u) ? 4.0 :
                                 (s->bpc == 12u) ? 16.0 :
                                 (s->bpc == 16u) ? 256.0 :
                                                   1.0;
    const double moment_scaler_sq = moment_scaler * moment_scaler;
    const double n_pixels = (double)s->width * (double)s->height;
    const double ref1 = ((double)s->h_sums[0] / moment_scaler) / n_pixels;
    const double dis1 = ((double)s->h_sums[1] / moment_scaler) / n_pixels;
    const double ref2 = ((double)s->h_sums[2] / moment_scaler_sq) / n_pixels;
    const double dis2 = ((double)s->h_sums[3] / moment_scaler_sq) / n_pixels;

    int err = vmaf_feature_collector_append_with_dict(feature_collector, s->feature_name_dict,
                                                      "float_moment_ref1st", ref1, index);
    if (!err) {
        err = vmaf_feature_collector_append_with_dict(feature_collector, s->feature_name_dict,
                                                      "float_moment_dis1st", dis1, index);
    }
    if (!err) {
        err = vmaf_feature_collector_append_with_dict(feature_collector, s->feature_name_dict,
                                                      "float_moment_ref2nd", ref2, index);
    }
    if (!err) {
        err = vmaf_feature_collector_append_with_dict(feature_collector, s->feature_name_dict,
                                                      "float_moment_dis2nd", dis2, index);
    }
    return err;
}

static int close_fex_sycl(VmafFeatureExtractor *fex)
{
    auto *s = static_cast<MomentStateSycl *>(fex->priv);
    if (s->sycl_state) {
        /* Unregister from the combined command graph before freeing priv.
         * Mirrors the fix in integer_motion_sycl.cpp (ADR-0989):
         * vmaf_sycl_graph_unregister() drains combined_queue and removes
         * this extractor's entry so a subsequent VmafContext sharing the
         * same sycl_state does not inherit a dangling priv pointer. */
        (void)vmaf_sycl_graph_unregister(s->sycl_state, s);

        if (s->d_sums)
            vmaf_sycl_free(s->sycl_state, s->d_sums);
        if (s->h_sums)
            vmaf_sycl_free(s->sycl_state, s->h_sums);
    }
    if (s->feature_name_dict)
        vmaf_dictionary_free(&s->feature_name_dict);
    return 0;
}

static const char *provided_features_moment_sycl[] = {
    "float_moment_ref1st",
    "float_moment_dis1st",
    "float_moment_ref2nd",
    "float_moment_dis2nd",
    nullptr,
};

// NOLINTEND(misc-use-anonymous-namespace, misc-use-internal-linkage)

extern "C" VmafFeatureExtractor vmaf_fex_float_moment_sycl = {
    .name = "float_moment_sycl",
    .init = init_fex_sycl,
    .extract = nullptr,
    .flush = nullptr,
    .close = close_fex_sycl,
    .submit = submit_fex_sycl,
    .collect = collect_fex_sycl,
    .options = options_moment_sycl,
    .priv_size = sizeof(MomentStateSycl),
    .flags = VMAF_FEATURE_EXTRACTOR_SYCL,
    .provided_features = provided_features_moment_sycl,
    .chars =
        {
            .n_dispatches_per_frame = 1,
            .is_reduction_only = true,
            .min_useful_frame_area = 1920U * 1080U,
            .dispatch_hint = VMAF_FEATURE_DISPATCH_AUTO,
        },
};

} /* extern "C" */
