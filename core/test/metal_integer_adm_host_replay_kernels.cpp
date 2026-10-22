/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * core/src/feature/metal/integer_adm.metal, unmodified, compiled for the host
 * through core/test/metal_msl_host_shim.h (ADR-1806), and one runner per
 * kernel that calls it at every thread position of a dispatch. A runner binds
 * what integer_adm_metal.mm's bind_stage_buffers() binds for that kernel, and
 * passes each buffer as the pointer type the kernel declares: a buffer of the
 * wrong element type is read the way the device reads it.
 *
 * The reductions run with one thread per threadgroup (thread 0, one thread):
 * their integer sums do not depend on the lane split, and a barrier is a
 * no-op. This translation unit holds nothing else, because the shim's
 * address-space macros rewrite whatever is included after it.
 */

#include "metal_integer_adm_host_replay.h"

#include "feature/metal/integer_adm.metal"

namespace
{

using Runner = void (*)(const IadmMetalStage *, int, const IadmDims &, const IadmCsf &,
                        const IadmReplayBuffers &);

template <typename T> T *view(uint8_t *bytes)
{
    return reinterpret_cast<T *>(bytes);
}

/* Every thread position of a 3D grid of tiles. */
template <typename F> void each_thread(const IadmMetalStage *s, F &&run)
{
    const uint nx = s->groups[0] * s->threads[0];
    const uint ny = s->groups[1] * s->threads[1];
    const uint nz = s->groups[2] * s->threads[2];
    for (uint z = 0; z < nz; ++z) {
        for (uint y = 0; y < ny; ++y) {
            for (uint x = 0; x < nx; ++x) {
                run(uint3{.x = x, .y = y, .z = z});
            }
        }
    }
}

/* Every threadgroup of a 1D grid, each as thread 0 of a one-thread group. */
template <typename F> void each_group(const IadmMetalStage *s, F &&run)
{
    for (uint wg = 0; wg < s->groups[0]; ++wg) {
        run(wg);
    }
}

void vert_8bpc(const IadmMetalStage *s, int, const IadmDims &d, const IadmCsf &c,
               const IadmReplayBuffers &b)
{
    each_thread(s, [&](uint3 g) {
        integer_adm_dwt_vert_8bpc(b.src_ref, b.src_dis, view<int>(b.dwt_tmp_ref),
                                  view<int>(b.dwt_tmp_dis), d, c, g);
    });
}

void vert_16bpc(const IadmMetalStage *s, int, const IadmDims &d, const IadmCsf &c,
                const IadmReplayBuffers &b)
{
    each_thread(s, [&](uint3 g) {
        integer_adm_dwt_vert_16bpc(view<ushort>(b.src_ref), view<ushort>(b.src_dis),
                                   view<int>(b.dwt_tmp_ref), view<int>(b.dwt_tmp_dis), d, c, g);
    });
}

void vert_s1(const IadmMetalStage *s, int scale, const IadmDims &d, const IadmCsf &c,
             const IadmReplayBuffers &b)
{
    each_thread(s, [&](uint3 g) {
        integer_adm_dwt_vert_s1(view<short>(b.ref_band[scale - 1]),
                                view<short>(b.dis_band[scale - 1]), view<int>(b.dwt_tmp_ref),
                                view<int>(b.dwt_tmp_dis), d, c, g);
    });
}

void vert_s123(const IadmMetalStage *s, int scale, const IadmDims &d, const IadmCsf &c,
               const IadmReplayBuffers &b)
{
    each_thread(s, [&](uint3 g) {
        integer_adm_dwt_vert_s123(view<int>(b.ref_band[scale - 1]),
                                  view<int>(b.dis_band[scale - 1]), view<int>(b.dwt_tmp_ref),
                                  view<int>(b.dwt_tmp_dis), d, c, g);
    });
}

void hori_s0(const IadmMetalStage *s, int scale, const IadmDims &d, const IadmCsf &c,
             const IadmReplayBuffers &b)
{
    each_thread(s, [&](uint3 g) {
        integer_adm_dwt_hori_s0(view<int>(b.dwt_tmp_ref), view<int>(b.dwt_tmp_dis),
                                view<short>(b.ref_band[scale]), view<short>(b.dis_band[scale]), d,
                                c, g);
    });
}

void hori_s123(const IadmMetalStage *s, int scale, const IadmDims &d, const IadmCsf &c,
               const IadmReplayBuffers &b)
{
    each_thread(s, [&](uint3 g) {
        integer_adm_dwt_hori_s123(view<int>(b.dwt_tmp_ref), view<int>(b.dwt_tmp_dis),
                                  view<int>(b.ref_band[scale]), view<int>(b.dis_band[scale]), d, c,
                                  g);
    });
}

void decouple_csf_s0(const IadmMetalStage *s, int scale, const IadmDims &d, const IadmCsf &c,
                     const IadmReplayBuffers &b)
{
    each_thread(s, [&](uint3 g) {
        integer_adm_decouple_csf_s0(view<short>(b.ref_band[scale]), view<short>(b.dis_band[scale]),
                                    view<short>(b.csf_a), view<short>(b.csf_f), d, c,
                                    uint2{.x = g.x, .y = g.y});
    });
}

void decouple_csf_s123(const IadmMetalStage *s, int scale, const IadmDims &d, const IadmCsf &c,
                       const IadmReplayBuffers &b)
{
    each_thread(s, [&](uint3 g) {
        integer_adm_decouple_csf_s123(view<int>(b.ref_band[scale]), view<int>(b.dis_band[scale]),
                                      view<int>(b.csf_a), view<int>(b.csf_f), d, c,
                                      uint2{.x = g.x, .y = g.y});
    });
}

void csf_cm_s0(const IadmMetalStage *s, int scale, const IadmDims &d, const IadmCsf &c,
               const IadmReplayBuffers &b)
{
    each_group(s, [&](uint wg) {
        integer_adm_csf_cm_s0(view<short>(b.ref_band[scale]), view<short>(b.dis_band[scale]),
                              view<short>(b.csf_f), view<uint>(b.accum[scale]), d, c, wg, 0u, 1u);
    });
}

void csf_cm_s123(const IadmMetalStage *s, int scale, const IadmDims &d, const IadmCsf &c,
                 const IadmReplayBuffers &b)
{
    each_group(s, [&](uint wg) {
        integer_adm_csf_cm_s123(view<int>(b.ref_band[scale]), view<int>(b.dis_band[scale]),
                                view<int>(b.csf_f), view<uint>(b.accum[scale]), d, c, wg, 0u, 1u);
    });
}

void aim_cm_s0(const IadmMetalStage *s, int scale, const IadmDims &d, const IadmCsf &c,
               const IadmReplayBuffers &b)
{
    each_group(s, [&](uint wg) {
        integer_adm_aim_cm_s0(view<short>(b.ref_band[scale]), view<short>(b.dis_band[scale]),
                              view<uint>(b.accum[scale]), d, c, wg, 0u, 1u);
    });
}

void aim_cm_s123(const IadmMetalStage *s, int scale, const IadmDims &d, const IadmCsf &c,
                 const IadmReplayBuffers &b)
{
    each_group(s, [&](uint wg) {
        integer_adm_aim_cm_s123(view<int>(b.ref_band[scale]), view<int>(b.dis_band[scale]),
                                view<uint>(b.accum[scale]), d, c, wg, 0u, 1u);
    });
}

/* In IadmMetalKernel order. */
const Runner RUNNERS[IADM_METAL_KERNEL_COUNT] = {
    vert_8bpc,       vert_16bpc,        vert_s1,   vert_s123,   hori_s0,   hori_s123,
    decouple_csf_s0, decouple_csf_s123, csf_cm_s0, csf_cm_s123, aim_cm_s0, aim_cm_s123,
};

} // namespace

extern "C" void iadm_replay_stage(const IadmMetalStage *stage, int scale, const IadmDims *d,
                                  const IadmCsf *c, const IadmReplayBuffers *b)
{
    if ((unsigned)stage->entry < (unsigned)IADM_METAL_KERNEL_COUNT) {
        RUNNERS[stage->entry](stage, scale, *d, *c, *b);
    }
}
