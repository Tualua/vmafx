- **SYCL: `n_subsample` above 1 no longer corrupts `integer_motion2` / `integer_motion3`.**
  On the frames `n_subsample` drops, libvmaf skips every SYCL extractor
  except the temporal `motion_sycl`, but the shared SYCL submit queue waited
  for every registered extractor before it enqueued a frame. Those frames ran
  no motion kernel: `motion_sycl` scored a stale SAD and the next frame
  differenced against a stale plane, so `integer_motion2` / `integer_motion3`
  went up to `motion_max_val` and `vmaf` read up to 100. Both the host-upload
  path (`vmaf --backend sycl --subsample N`) and the QSV zero-copy path of the
  FFmpeg `libvmaf_sycl` filter were affected. libvmaf now reports each
  skipped SYCL extractor to the SYCL state, which enqueues the frame for the
  extractors that did submit; scores at `n_subsample` 2 and 4 equal the
  CPU's, and `n_subsample` 1 is unchanged
  (`T-SYCL-ZEROCOPY-NSUBSAMPLE-MOTION-2026-10-06`).
