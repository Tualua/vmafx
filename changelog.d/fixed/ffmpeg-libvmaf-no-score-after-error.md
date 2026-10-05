- **The FFmpeg `libvmaf` and `libvmaf_cuda` filters no longer print a score
  after a mid-run error.** When a frame could not be copied or read, the run
  failed, but the filter still printed a `VMAF score:` line pooled over fewer
  frames than were decoded. Usually that line was an uninitialised
  `0.000000`. The filter now logs one error naming the frame and the cause,
  exits non-zero, and prints no score and writes no report. A failed flush or
  a failed pooled score prints no score line either. This differs from
  upstream FFmpeg on purpose
  ([ADR-1768](docs/adr/1768-ffmpeg-libvmaf-no-score-after-error.md)).
