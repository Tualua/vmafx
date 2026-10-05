/**
 *  Copyright 2016-2026 Netflix, Inc.
 *  Copyright 2026 Lusoris
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 *  float_motion feature kernel on the SYCL backend (T7-23 / batch 3
 *  part 4c — ADR-0192 / ADR-0196). SYCL twin of float_motion_vulkan
 *  + float_motion_cuda. Self-contained submit/collect.
 *
 *  Score options follow CPU float_motion.c on the host: every emitted
 *  `motion` / `motion2` value is motion_clip()ped, i.e. scaled by
 *  `motion_fps_weight` and capped at `motion_max_val`, and
 *  `motion_force_zero` publishes zeros without touching the device.
 *  `motion3` is the CPU's too: the motion2 value motion_blend_clip()ped (fps
 *  weight, the `motion_blend_factor` / `motion_blend_offset` blend, the cap),
 *  frame 0 from the first SAD, the last frame from flush(), and 0 for a
 *  one-frame run, as float_motion.c emits it
 *  (T-GPU-FLOAT-MOTION3-MISSING-2026-09-30).
 *
 *  `motion_add_uv` (ADR-1767) runs the same blur and row-SAD chain on Cb and Cr at
 *  their own size, from the shared chroma planes, and adds the per-plane scores
 *  in double as float_motion.c::motion_score_pair() does (Y, then U, then V).
 *
 *  Numerical contract (ADR-1367, ADR-1409, ADR-1411). Both steps are the CPU's
 *  arithmetic, so the twin returns the CPU extractor's score bit for bit:
 *   - the blur is convolution_f32_c_s(): each tap one rounded fp32 multiply
 *     and one rounded fp32 add, taps in order, vertical pass then
 *     horizontal. The TU builds with contraction off, so nothing is fused.
 *   - the SAD is compute_motion_simd(): float_sad_line() adds the absolute
 *     differences of one row, left to right, into one fp32 accumulator, and
 *     the rows are added top to bottom into another. The result depends on
 *     that order (at 1920x1080 it is up to 1.4e-4 from the exact sum), so
 *     the row kernel runs one work-item per row over the whole row and the
 *     host adds the rows (feature/float_motion_sad.h). A per-group partial
 *     sum, which this file used to produce, cannot give that value.
 */

#include <sycl/sycl.hpp>

#include "sycl_compat.h"
#include "sycl_tile_index.h"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <utility>

#include "config.h"
#include "feature_collector.h"
#include "feature_extractor.h"
#include "feature/float_motion_sad.h"
#include "feature_name.h"
#include "log.h"
#include "motion_blend_tools.h"
#include "picture.h"
#include "sycl/common.h"

namespace
{

/* One picture plane the score reads: its size, the blur ping-pong and where its
 * row sums sit in the shared row-SAD buffer. */
struct FmPlane {
    unsigned w;
    unsigned h;
    float *d_blur[2];
    size_t row_off;
};

constexpr unsigned FM_MAX_PLANES = 3;

struct FloatMotionStateSycl {
    bool debug;
    bool motion_force_zero;
    double motion_fps_weight;
    double motion_blend_factor;
    double motion_blend_offset;
    double motion_max_val;
    bool motion_add_uv;

    unsigned width;
    unsigned height;
    unsigned bpc;

    VmafSyclState *sycl_state;

    /* Y, and U and V with motion_add_uv; the blurred refs ping-pong per plane. */
    FmPlane plane[FM_MAX_PLANES];
    unsigned n_planes;
    int cur_blur;

    /* float_sad_line() of every row of every plane: `row_count` fp32 sums. */
    size_t row_count;
    float *d_row_sad;
    float *h_row_sad;

    bool has_pending;
    unsigned pending_index;
    unsigned frame_index;
    double prev_motion_score;

    VmafDictionary *feature_name_dict;
};

} // namespace

namespace
{

static constexpr int FM_WG_X = 32;
static constexpr int FM_WG_Y = 4;
static constexpr int FM_HALF = 2;
static constexpr int FM_TILE_W = FM_WG_X + 2 * FM_HALF; /* 36 */
static constexpr int FM_TILE_H = FM_WG_Y + 2 * FM_HALF; /* 8  */

/* CPU float_motion.c DEFAULT_MOTION_MAX_VAL. */
static constexpr double FM_DEFAULT_MAX_VAL = 10000.0;

static constexpr float FM_FILT[5] = {
    0.054488685f, 0.244201342f, 0.402619947f, 0.244201342f, 0.054488685f,
};

struct FmKernelArgs {
    const void *ref;
    float *cur_blur;
    unsigned width;
    unsigned height;
    unsigned bpc;
};

struct FmRowSadArgs {
    const float *cur_blur;
    const float *prev_blur;
    float *row_sad;
    unsigned width;
    unsigned height;
};

} // namespace

namespace
{

static inline int dev_mirror_fm(int idx, int sup)
{
    if (idx < 0) {
        return -idx;
    }
    if (idx >= sup) {
        return 2 * (sup - 1) - idx;
    }
    return idx;
}

static inline float fm_inv_scaler(unsigned bpc)
{
    float scaler = 1.0f;
    if (bpc == 10) {
        scaler = 4.0f;
    } else if (bpc == 12) {
        scaler = 16.0f;
    } else if (bpc == 16) {
        scaler = 256.0f;
    }
    return 1.0f / scaler;
}

} // namespace

namespace
{

static inline float fm_read_pixel(const FmKernelArgs &args, int y, int x, float inv_scaler)
{
    const size_t offset = (size_t)y * args.width + (size_t)x;
    if (args.bpc <= 8) {
        const uint8_t value = static_cast<const uint8_t *>(args.ref)[offset];
        return (float)value - 128.0f;
    }
    const uint16_t value = static_cast<const uint16_t *>(args.ref)[offset];
    return (float)value * inv_scaler - 128.0f;
}

} // namespace

namespace
{

static inline void fm_load_tile(sycl::nd_item<2> item, const sycl::local_accessor<float, 2> &tile,
                                const FmKernelArgs &args, float inv_scaler)
{
    const unsigned lid = item.get_local_linear_id();
    const int tile_y = (int)(item.get_group(0) * FM_WG_Y) - FM_HALF;
    const int tile_x = (int)(item.get_group(1) * FM_WG_X) - FM_HALF;
    const bool interior = (tile_y >= 0) && (tile_y + FM_TILE_H <= (int)args.height) &&
                          (tile_x >= 0) && (tile_x + FM_TILE_W <= (int)args.width);
    constexpr unsigned tile_elems = FM_TILE_H * FM_TILE_W;
    constexpr unsigned group_size = FM_WG_X * FM_WG_Y;
    for (unsigned i = lid; i < tile_elems; i += group_size) {
        const unsigned row = i / FM_TILE_W;
        const unsigned col = i % FM_TILE_W;
        int pixel_y = tile_y + (int)row;
        int pixel_x = tile_x + (int)col;
        if (!interior) {
            pixel_y =
                vmaf_sycl_tile_index(dev_mirror_fm(pixel_y, (int)args.height), (int)args.height);
            pixel_x =
                vmaf_sycl_tile_index(dev_mirror_fm(pixel_x, (int)args.width), (int)args.width);
        }
        tile[row][col] = fm_read_pixel(args, pixel_y, pixel_x, inv_scaler);
    }
}

} // namespace

namespace
{

static inline void fm_filter_vertical(sycl::nd_item<2> item,
                                      const sycl::local_accessor<float, 2> &tile,
                                      const sycl::local_accessor<float, 2> &vertical)
{
    constexpr unsigned group_size = FM_WG_X * FM_WG_Y;
    for (unsigned i = item.get_local_linear_id(); i < (unsigned)(FM_WG_Y * FM_TILE_W);
         i += group_size) {
        const unsigned row = i / FM_TILE_W;
        const unsigned col = i % FM_TILE_W;
        float sum = 0.0f;
        for (int k = 0; k < 5; k++) {
            sum += FM_FILT[k] * tile[row + k][col];
        }
        vertical[row][col] = sum;
    }
}

} // namespace

namespace
{

static inline void fm_filter_horizontal(sycl::nd_item<2> item,
                                        const sycl::local_accessor<float, 2> &vertical,
                                        const FmKernelArgs &args)
{
    const int x = (int)item.get_global_id(1);
    const int y = (int)item.get_global_id(0);
    if (!std::cmp_less(x, args.width) || !std::cmp_less(y, args.height)) {
        return;
    }
    const unsigned local_x = item.get_local_id(1);
    const unsigned local_y = item.get_local_id(0);
    float blurred = 0.0f;
    for (int k = 0; k < 5; k++) {
        blurred += FM_FILT[k] * vertical[local_y][local_x + k];
    }
    args.cur_blur[(size_t)y * args.width + (size_t)x] = blurred;
}

} // namespace

namespace
{

/* float_sad_line() of row `y`: the absolute differences added left to right
 * into one fp32 accumulator. The order is the result (ADR-1409); do not
 * split, stride or reduce this loop. One scalar accumulator and two USM
 * pointers, so the kernel needs no scratch memory (ADR-1395). */
static inline float fm_row_sad(const FmRowSadArgs &args, size_t y)
{
    const float *cur = args.cur_blur + y * args.width;
    const float *prev = args.prev_blur + y * args.width;
    float accum = 0.0f;
    for (unsigned j = 0; j < args.width; j++) {
        const float diff = cur[j] - prev[j];
        accum += diff < 0.0f ? -diff : diff;
    }
    return accum;
}

} // namespace

namespace
{

static sycl::event launch_float_motion(sycl::queue &q, const FmKernelArgs &args)
{
    const size_t global_x = ((static_cast<size_t>(args.width) + FM_WG_X - 1) / FM_WG_X) * FM_WG_X;
    const size_t global_y = ((static_cast<size_t>(args.height) + FM_WG_Y - 1) / FM_WG_Y) * FM_WG_Y;
    return q.submit([&](sycl::handler &cgh) {
        sycl::local_accessor<float, 2> const s_tile(sycl::range<2>(FM_TILE_H, FM_TILE_W), cgh);
        sycl::local_accessor<float, 2> const s_vert(sycl::range<2>(FM_WG_Y, FM_TILE_W), cgh);

        cgh.parallel_for(
            sycl::nd_range<2>(sycl::range<2>(global_y, global_x), sycl::range<2>(FM_WG_Y, FM_WG_X)),
            [=](sycl::nd_item<2> item) VMAF_SYCL_REQD_SG_SIZE(32) {
                fm_load_tile(item, s_tile, args, fm_inv_scaler(args.bpc));
                item.barrier(sycl::access::fence_space::local_space);
                fm_filter_vertical(item, s_tile, s_vert);
                item.barrier(sycl::access::fence_space::local_space);
                fm_filter_horizontal(item, s_vert, args);
            });
    });
}

/* One work-item per row; `row_sad` receives `height` sums.
 *
 * Sub-group size 16: the lanes of a hardware thread are rows, so each loop
 * step is two gathers, and the pass is bound by reading both blurred planes
 * again. Narrow sub-groups put more threads on that. Measured on an Arc A380
 * at 3840x2160 (ADR-1411), time of this kernel per frame: 0.70 ms at 8,
 * 0.82 at 16, 1.12 at the compiler's choice (32). 8 is not available: Xe2
 * targets of the default AOT list do not compile a kernel that requires it
 * (ADR-1468), so 16 is the narrowest size every target takes. Two other
 * exact shapes were no faster: a sub-group of 16 consecutive pixels per row
 * summed lane by lane with select_from_group() (1.10 ms), and one work-group
 * per row with 16-wide loads and a lane-uniform chain (0.80 ms). */
static sycl::event launch_float_motion_row_sad(sycl::queue &q, const FmRowSadArgs &args)
{
    return q.submit([&](sycl::handler &cgh) {
        cgh.parallel_for(sycl::range<1>(args.height),
                         [=](sycl::id<1> row) VMAF_SYCL_REQD_SG_SIZE(16) {
                             args.row_sad[row[0]] = fm_row_sad(args, row[0]);
                         });
    });
}

} // namespace

namespace
{

static const VmafOption options_float_motion_sycl[] = {
    {.name = "debug",
     .help = "debug mode: enable additional output",
     .offset = offsetof(FloatMotionStateSycl, debug),
     .type = VMAF_OPT_TYPE_BOOL,
     .default_val = {.b = true}},
    {.name = "motion_force_zero",
     .help = "force motion score to zero",
     .alias = "force_0",
     .offset = offsetof(FloatMotionStateSycl, motion_force_zero),
     .type = VMAF_OPT_TYPE_BOOL,
     .default_val = {.b = false},
     .flags = VMAF_OPT_FLAG_FEATURE_PARAM},
    {.name = "motion_fps_weight",
     .help = "fps-aware multiplicative weight/correction",
     .alias = "mfw",
     .offset = offsetof(FloatMotionStateSycl, motion_fps_weight),
     .type = VMAF_OPT_TYPE_DOUBLE,
     .default_val = {.d = 1.0},
     .min = 0.0,
     .max = 5.0,
     .flags = VMAF_OPT_FLAG_FEATURE_PARAM},
    /* The motion3 blend, declared as in the CPU table (name, alias, default,
     * range, flags); a non-default value names the features
     * (motion3_mbf_0.5_mbo_2). */
    {.name = "motion_blend_factor",
     .help = "blend motion score given an offset",
     .alias = "mbf",
     .offset = offsetof(FloatMotionStateSycl, motion_blend_factor),
     .type = VMAF_OPT_TYPE_DOUBLE,
     .default_val = {.d = 1.0},
     .min = 0.0,
     .max = 1.0,
     .flags = VMAF_OPT_FLAG_FEATURE_PARAM},
    {.name = "motion_blend_offset",
     .help = "blend motion score starting from this offset",
     .alias = "mbo",
     .offset = offsetof(FloatMotionStateSycl, motion_blend_offset),
     .type = VMAF_OPT_TYPE_DOUBLE,
     .default_val = {.d = 40.0},
     .min = 0.0,
     .max = 1000.0,
     .flags = VMAF_OPT_FLAG_FEATURE_PARAM},
    {.name = "motion_add_uv",
     .help = "include U and V terms",
     .alias = "mau",
     .offset = offsetof(FloatMotionStateSycl, motion_add_uv),
     .type = VMAF_OPT_TYPE_BOOL,
     .default_val = {.b = false},
     .flags = VMAF_OPT_FLAG_FEATURE_PARAM},
    {.name = "motion_max_val",
     .help = "maximum value allowed; larger values will be clipped to this value",
     .alias = "mmxv",
     .offset = offsetof(FloatMotionStateSycl, motion_max_val),
     .type = VMAF_OPT_TYPE_DOUBLE,
     .default_val = {.d = FM_DEFAULT_MAX_VAL},
     .min = 0.0,
     .max = 10000.0,
     .flags = VMAF_OPT_FLAG_FEATURE_PARAM},
    {.name = nullptr}};

} // namespace

namespace
{

static int allocate_motion_buffers(FloatMotionStateSycl *s)
{
    VmafSyclState *state = s->sycl_state;
    for (unsigned c = 0; c < s->n_planes; c++) {
        FmPlane &p = s->plane[c];
        const size_t blur_bytes = (size_t)p.w * p.h * sizeof(float);
        p.d_blur[0] = static_cast<float *>(vmaf_sycl_malloc_device(state, blur_bytes));
        p.d_blur[1] = static_cast<float *>(vmaf_sycl_malloc_device(state, blur_bytes));
        if (!p.d_blur[0] || !p.d_blur[1]) {
            vmaf_log(VMAF_LOG_LEVEL_ERROR, "float_motion_sycl: USM allocation failed\n");
            return -ENOMEM;
        }
    }
    const size_t sad_bytes = s->row_count * sizeof(float);
    s->d_row_sad = static_cast<float *>(vmaf_sycl_malloc_device(state, sad_bytes));
    s->h_row_sad = static_cast<float *>(vmaf_sycl_malloc_host(state, sad_bytes));
    if (!s->d_row_sad || !s->h_row_sad) {
        vmaf_log(VMAF_LOG_LEVEL_ERROR, "float_motion_sycl: USM allocation failed\n");
        return -ENOMEM;
    }
    return 0;
}

} // namespace

namespace
{

static int close_fex_sycl(VmafFeatureExtractor *fex);

/* Cb / Cr size at picture.c's ceiling geometry, (dim + ss) >> ss. */
static int fm_chroma_size(enum VmafPixelFormat pix_fmt, unsigned w, unsigned h, unsigned *cw,
                          unsigned *ch)
{
    switch (pix_fmt) {
    case VMAF_PIX_FMT_YUV420P:
        *cw = (w + 1u) >> 1u;
        *ch = (h + 1u) >> 1u;
        return 0;
    case VMAF_PIX_FMT_YUV422P:
        *cw = (w + 1u) >> 1u;
        *ch = h;
        return 0;
    case VMAF_PIX_FMT_YUV444P:
        *cw = w;
        *ch = h;
        return 0;
    default:
        return -EINVAL;
    }
}

/* The 5-tap SYCL float_motion kernel uses reflect-101 mirror padding;
 * dev_mirror_fm() returns 2*sup - idx - 2, which is negative when sup < 3.
 * Refuse smaller planes up front to prevent out-of-bounds device reads (the
 * CPU's motion_check_min_dim_all_planes() checks every plane it convolves).
 * Minimum: filter_width/2 + 1 = 3. */
static int fm_check_min_dim(unsigned w, unsigned h, const char *plane)
{
    if (h >= 3u && w >= 3u) {
        return 0;
    }
    vmaf_log(VMAF_LOG_LEVEL_ERROR,
             "float_motion_sycl: %s plane %ux%u is below the 5-tap filter minimum 3x3; "
             "refusing to avoid out-of-bounds mirror reads on device\n",
             plane, w, h);
    return -EINVAL;
}

/* Geometry of every plane the options ask for, and where each plane's row sums
 * sit in the row-SAD buffer. Touches no device object. */
static int fm_plane_layout(FloatMotionStateSycl *s, enum VmafPixelFormat pix_fmt, unsigned w,
                           unsigned h)
{
    s->n_planes = 1u;
    s->plane[0] = {.w = w, .h = h, .d_blur = {nullptr, nullptr}, .row_off = 0u};
    s->row_count = h;
    int err = fm_check_min_dim(w, h, "luma");
    if (err || !s->motion_add_uv) {
        return err;
    }
    unsigned cw = 0;
    unsigned ch = 0;
    err = fm_chroma_size(pix_fmt, w, h, &cw, &ch);
    if (err) {
        vmaf_log(VMAF_LOG_LEVEL_ERROR,
                 "float_motion_sycl: motion_add_uv needs a pixel format with chroma planes\n");
        return err;
    }
    err = fm_check_min_dim(cw, ch, "chroma");
    if (err) {
        return err;
    }
    for (unsigned c = 1; c < FM_MAX_PLANES; c++) {
        s->plane[c] = {.w = cw, .h = ch, .d_blur = {nullptr, nullptr}, .row_off = s->row_count};
        s->row_count += ch;
    }
    s->n_planes = FM_MAX_PLANES;
    return 0;
}

/* Luma and chroma come from the shared device planes; both calls are idempotent
 * across the twins that share them (ADR-1766, ADR-1369). */
static int fm_init_shared_planes(const FloatMotionStateSycl *s)
{
    int err = vmaf_sycl_shared_frame_init(s->sycl_state, s->width, s->height, s->bpc);
    if (!err && s->n_planes > 1u) {
        err = vmaf_sycl_shared_chroma_init(s->sycl_state, s->plane[1].w, s->plane[1].h);
    }
    return err;
}

static int init_fex_sycl(VmafFeatureExtractor *fex, enum VmafPixelFormat pix_fmt, unsigned bpc,
                         unsigned w, unsigned h)
{
    auto *s = static_cast<FloatMotionStateSycl *>(fex->priv);

    const int layout_err = fm_plane_layout(s, pix_fmt, w, h);
    if (layout_err) {
        return layout_err;
    }

    s->width = w;
    s->height = h;
    s->bpc = bpc;
    s->frame_index = 0;
    s->prev_motion_score = 0.0;
    s->cur_blur = 0;
    s->has_pending = false;

    if (!fex->sycl_state) {
        vmaf_log(VMAF_LOG_LEVEL_ERROR, "float_motion_sycl: no SYCL state\n");
        return -EINVAL;
    }
    s->sycl_state = fex->sycl_state;
    int err = fm_init_shared_planes(s);
    if (!err) {
        err = allocate_motion_buffers(s);
    }
    if (err) {
        (void)close_fex_sycl(fex);
        return err;
    }

    s->feature_name_dict =
        vmaf_feature_name_dict_from_provided_features(fex->provided_features, fex->options, s);
    if (!s->feature_name_dict) {
        (void)close_fex_sycl(fex);
        return -ENOMEM;
    }
    return 0;
}

} // namespace

namespace
{

/* Makes this frame's planes current and orders the primary queue after their
 * upload. Chroma is uploaded here from host pictures, or is valid only when a
 * zero-copy import marked it for this frame (ADR-1765). */
static int fm_ready_planes(FloatMotionStateSycl *s, VmafPicture *ref_pic, VmafPicture *dist_pic,
                           unsigned index, sycl::queue *q)
{
    int err = 0;
    if (s->n_planes > 1u) {
        if (vmaf_sycl_require_chroma(s->sycl_state, "float_motion_sycl", ref_pic, dist_pic)) {
            return -ENOTSUP;
        }
        if (ref_pic && dist_pic) {
            err = vmaf_sycl_shared_chroma_upload(s->sycl_state, ref_pic, dist_pic);
        }
    }
    if (!err) {
        err = vmaf_sycl_queue_after_upload(s->sycl_state, q);
    }
    if (err) {
        vmaf_log(VMAF_LOG_LEVEL_ERROR, "float_motion_sycl: frame %u plane upload failed (%d)\n",
                 index, err);
    }
    return err;
}

/* Blur of every plane into its current slot, then each plane's row SADs against
 * the previous slot; one device-to-host copy of all the row sums. */
static int fm_enqueue_planes(FloatMotionStateSycl *s, sycl::queue &q)
{
    const unsigned cur_idx = (unsigned)s->cur_blur;
    const unsigned prev_idx = 1u - cur_idx;
    for (unsigned c = 0; c < s->n_planes; c++) {
        const FmPlane &p = s->plane[c];
        /* The reference plane of this frame; the blur is what carries over to the
         * next frame, in d_blur, so the slot is read once and never kept. */
        const void *ref = vmaf_sycl_get_shared_plane(s->sycl_state, 1, c);
        if (!ref) {
            return -EINVAL;
        }
        launch_float_motion(q, {.ref = ref,
                                .cur_blur = p.d_blur[cur_idx],
                                .width = p.w,
                                .height = p.h,
                                .bpc = s->bpc});
        if (s->frame_index > 0) {
            /* The first frame has no previous blur: no SAD, nothing to read. */
            launch_float_motion_row_sad(q, {.cur_blur = p.d_blur[cur_idx],
                                            .prev_blur = p.d_blur[prev_idx],
                                            .row_sad = s->d_row_sad + p.row_off,
                                            .width = p.w,
                                            .height = p.h});
        }
    }
    if (s->frame_index > 0) {
        q.memcpy(s->h_row_sad, s->d_row_sad, s->row_count * sizeof(float));
    }
    return 0;
}

static int submit_fex_sycl(VmafFeatureExtractor *fex, VmafPicture *ref_pic, VmafPicture *ref_pic_90,
                           VmafPicture *dist_pic, VmafPicture *dist_pic_90, unsigned index)
{
    (void)ref_pic_90;
    (void)dist_pic_90;
    auto *s = static_cast<FloatMotionStateSycl *>(fex->priv);
    if (s->motion_force_zero) {
        /* CPU float_motion.c publishes zeros without computing a SAD. */
        s->pending_index = index;
        s->has_pending = true;
        return 0;
    }
    auto *qptr = static_cast<sycl::queue *>(vmaf_sycl_get_queue_ptr(s->sycl_state));
    if (!qptr) {
        return -EINVAL;
    }
    /* The frame's planes are already in the shared planes (the host read path
     * uploads them before any extractor submits, the zero-copy import writes
     * them there), packed at width * bytes per sample (ADR-1766). */
    int err = fm_ready_planes(s, ref_pic, dist_pic, index, qptr);
    if (!err) {
        err = fm_enqueue_planes(s, *qptr);
    }
    if (err) {
        return err;
    }

    s->pending_index = index;
    s->has_pending = true;
    return 0;
}

} // namespace

namespace
{

/* CPU float_motion.c::motion_clip: fps weight, then the motion_max_val cap. */
static double motion_clip(const FloatMotionStateSycl *s, double score)
{
    const double weighted = score * s->motion_fps_weight;
    return weighted < s->motion_max_val ? weighted : s->motion_max_val;
}

/* CPU float_motion.c::motion_blend_clip (motion3): fps weight, the blend,
 * then the motion_max_val cap. */
static double motion_blend_clip(const FloatMotionStateSycl *s, double score)
{
    const double blended =
        motion_blend(score * s->motion_fps_weight, s->motion_blend_factor, s->motion_blend_offset);
    return blended < s->motion_max_val ? blended : s->motion_max_val;
}

static int motion_append(const FloatMotionStateSycl *s, VmafFeatureCollector *feature_collector,
                         const char *name, double score, unsigned index)
{
    return vmaf_feature_collector_append_with_dict(feature_collector, s->feature_name_dict, name,
                                                   score, index);
}

/* CPU float_motion.c::motion_append_forced_zero. */
static int motion_append_forced_zero(const FloatMotionStateSycl *s,
                                     VmafFeatureCollector *feature_collector, unsigned index)
{
    int err = motion_append(s, feature_collector, "VMAF_feature_motion2_score", 0.0, index);
    if (!err) {
        err = motion_append(s, feature_collector, "VMAF_feature_motion3_score", 0.0, index);
    }
    if (s->debug && !err) {
        err = motion_append(s, feature_collector, "VMAF_feature_motion_score", 0.0, index);
    }
    return err;
}

} // namespace

namespace
{

/* CPU float_motion.c::extract() at index 0: no previous frame, so motion2 and
 * the debug score are 0; motion3 of frame 0 waits for the second frame's SAD
 * or, for a one-frame run, for flush(). */
static int motion_emit_first(const FloatMotionStateSycl *s, VmafFeatureCollector *feature_collector,
                             unsigned index)
{
    int err = motion_append(s, feature_collector, "VMAF_feature_motion2_score", 0.0, index);
    if (s->debug && !err) {
        err = motion_append(s, feature_collector, "VMAF_feature_motion_score", 0.0, index);
    }
    return err;
}

/* The scores frame `index`'s SAD completes, as CPU float_motion.c::extract()
 * emits them: the debug score at `index`; at the second frame motion3 of
 * frame 0 from this first SAD alone (its motion2 is the 0 emitted before);
 * afterwards motion2 / motion3 of the previous frame from the smaller of its
 * two SADs, motion_clip()ped / motion_blend_clip()ped. */
static int motion_emit(const FloatMotionStateSycl *s, VmafFeatureCollector *feature_collector,
                       unsigned index, double motion_score)
{
    int err = 0;
    if (s->debug) {
        err = motion_append(s, feature_collector, "VMAF_feature_motion_score",
                            motion_clip(s, motion_score), index);
    }
    if (s->frame_index == 1) {
        if (!err) {
            err = motion_append(s, feature_collector, "VMAF_feature_motion3_score",
                                motion_blend_clip(s, motion_score), index - 1);
        }
        return err;
    }
    const double motion2 =
        motion_score < s->prev_motion_score ? motion_score : s->prev_motion_score;
    if (!err) {
        err = motion_append(s, feature_collector, "VMAF_feature_motion2_score",
                            motion_clip(s, motion2), index - 1);
    }
    if (!err) {
        err = motion_append(s, feature_collector, "VMAF_feature_motion3_score",
                            motion_blend_clip(s, motion2), index - 1);
    }
    return err;
}

} // namespace

namespace
{

/* compute_motion()'s per-plane scores added in double, Y then U then V, as
 * motion_score_pair() adds them. */
static double frame_sad_score(const FloatMotionStateSycl *s)
{
    double total = 0.0;
    for (unsigned c = 0; c < s->n_planes; c++) {
        const FmPlane &p = s->plane[c];
        const double plane_score =
            vmaf_float_motion_score_from_row_sads(s->h_row_sad + p.row_off, p.w, p.h);
        total = c == 0 ? plane_score : total + plane_score;
    }
    return total;
}

static int collect_fex_sycl(VmafFeatureExtractor *fex, unsigned index,
                            VmafFeatureCollector *feature_collector)
{
    auto *s = static_cast<FloatMotionStateSycl *>(fex->priv);
    if (s->motion_force_zero) {
        return motion_append_forced_zero(s, feature_collector, index);
    }
    auto *qptr = static_cast<sycl::queue *>(vmaf_sycl_get_queue_ptr(s->sycl_state));
    if (!qptr) {
        return -EINVAL;
    }
    qptr->wait();

    if (s->frame_index == 0) {
        const int err = motion_emit_first(s, feature_collector, index);
        s->cur_blur = 1 - s->cur_blur;
        s->frame_index++;
        return err;
    }

    /* compute_motion_simd()'s tail: the rows top to bottom into one float and
     * the float division (ADR-1409). */
    const double motion_score = frame_sad_score(s);
    const int err = motion_emit(s, feature_collector, index, motion_score);

    s->prev_motion_score = motion_score;
    s->cur_blur = 1 - s->cur_blur;
    s->frame_index++;
    return err;
}

} // namespace

namespace
{

/* CPU float_motion.c::flush: the tail motion2 / motion3 are motion_clip() /
 * motion_blend_clip() of the last SAD at the last frame index; a run without
 * a SAD (one frame) has motion3 = 0 at index 0. */
static int flush_fex_sycl(VmafFeatureExtractor *fex, VmafFeatureCollector *feature_collector)
{
    auto const *s = static_cast<FloatMotionStateSycl *>(fex->priv);
    int ret = 0;
    if (s->motion_force_zero) {
        return 1;
    }

    if (s->frame_index > 1) {
        const unsigned last = s->frame_index - 1;
        ret = motion_append(s, feature_collector, "VMAF_feature_motion2_score",
                            motion_clip(s, s->prev_motion_score), last);
        if (!ret) {
            ret = motion_append(s, feature_collector, "VMAF_feature_motion3_score",
                                motion_blend_clip(s, s->prev_motion_score), last);
        }
    } else {
        ret = motion_append(s, feature_collector, "VMAF_feature_motion3_score", 0.0, 0);
    }
    return (ret < 0) ? ret : !ret;
}

} // namespace

namespace
{

static void free_motion_buffers(FloatMotionStateSycl *s)
{
    if (!s->sycl_state) {
        return;
    }
    for (FmPlane &p : s->plane) {
        for (float *&blur : p.d_blur) {
            if (blur) {
                vmaf_sycl_free(s->sycl_state, blur);
                blur = nullptr;
            }
        }
    }
    if (s->d_row_sad) {
        vmaf_sycl_free(s->sycl_state, s->d_row_sad);
    }
    if (s->h_row_sad) {
        vmaf_sycl_free(s->sycl_state, s->h_row_sad);
    }
}

static int close_fex_sycl(VmafFeatureExtractor *fex)
{
    auto *s = static_cast<FloatMotionStateSycl *>(fex->priv);
    free_motion_buffers(s);
    if (s->feature_name_dict)
        vmaf_dictionary_free(&s->feature_name_dict);
    return 0;
}

static const char *provided_features_float_motion_sycl[] = {"VMAF_feature_motion_score",
                                                            "VMAF_feature_motion2_score",
                                                            "VMAF_feature_motion3_score", nullptr};

} // namespace

/* The zero-copy admission hook (ADR-1688), widened by ADR-1768: this twin
 * reads only the shared device planes, luma and chroma, which the zero-copy
 * import fills (ADR-1765, ADR-1766), so it runs on that path for any options. */
static bool reads_shared_luma_only(const VmafFeatureExtractor * /*fex*/)
{
    return true;
}

extern "C" VmafFeatureExtractor vmaf_fex_float_motion_sycl = {
    .name = "float_motion_sycl",
    .init = init_fex_sycl,
    .extract = nullptr,
    .flush = flush_fex_sycl,
    .close = close_fex_sycl,
    .submit = submit_fex_sycl,
    .collect = collect_fex_sycl,
    .options = options_float_motion_sycl,
    .priv_size = sizeof(FloatMotionStateSycl),
    .flags = VMAF_FEATURE_EXTRACTOR_TEMPORAL | VMAF_FEATURE_EXTRACTOR_SYCL,
    .provided_features = provided_features_float_motion_sycl,
    .reads_shared_luma_only = reads_shared_luma_only,
};
