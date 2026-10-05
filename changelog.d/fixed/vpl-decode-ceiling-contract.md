- **VPL decode retry ceiling contract and warning frame drop repair**:
  `vmaf_vpl` decode retry loop is formally verified under the 60,000-attempt
  bound on physical Intel Arc A380 hardware and hermetic unit tests. Frames
  published alongside warning status codes (`sts > 0 && sync != NULL`, e.g.
  `MFX_WRN_VIDEO_PARAM_CHANGED`) are now delivered instead of dropped, and
  transient `MFX_WRN_ALLOC_TIMEOUT_EXPIRED` retries cleanly.
