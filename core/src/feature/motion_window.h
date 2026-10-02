/**
 *
 *  Copyright 2026 Lusoris
 *
 * SPDX-License-Identifier: EUPL-1.2
 */

/*
 * The temporal window of the integer motion extractors (ADR-1478).
 *
 * Every integer motion extractor stores one SAD score per frame and derives
 * motion2 and motion3 of all frames from those scores once the last frame is
 * in. The derivation is integer_motion.c::flush() (Netflix a4a1492d), with
 * the three-frame window or, with motion_five_frame_window, the five-frame
 * one. It is defined once, in integer_motion.c, and called by the CPU
 * extractors `motion` and `motion_v2` and by the GPU twins that have the
 * five-frame window, so a twin's scores are the CPU's whenever its SAD scores
 * are.
 */

#ifndef FEATURE_MOTION_WINDOW_H_
#define FEATURE_MOTION_WINDOW_H_

#include <stdbool.h>

#include "dict.h"
#include "feature_collector.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What one extractor's flush needs: the provided-feature keys it writes (the
 * collector names are looked up in its feature-name dictionary, so option
 * suffixes are applied) and the options that shape the window. */
typedef struct VmafMotionWindow {
    const char *sad_feature;     /* per-frame SAD score, read */
    const char *motion2_feature; /* written for every frame */
    const char *motion3_feature; /* written for every frame */
    double motion_blend_factor;
    double motion_blend_offset;
    double motion_max_val;
    bool motion_five_frame_window;
    bool motion_moving_average;
} VmafMotionWindow;

/*
 * Append motion2 and motion3 for every frame that has a SAD score.
 *
 * The SAD scores are those the extractor appended under `sad_feature`:
 * weighted by motion_fps_weight and capped at motion_max_val, 0 for the
 * frames without an earlier frame to difference against. A collector without
 * any SAD score gets nothing appended.
 *
 * Returns 0, -EINVAL when the dictionary lacks `sad_feature`, or the error of
 * the first append that fails.
 */
int vmaf_motion_window_flush(VmafFeatureCollector *feature_collector,
                             VmafDictionary *feature_name_dict, const VmafMotionWindow *window);

#ifdef __cplusplus
}
#endif

#endif /* FEATURE_MOTION_WINDOW_H_ */
