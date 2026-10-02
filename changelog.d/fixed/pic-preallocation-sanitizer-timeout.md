- **The ASan + UBSan job no longer kills `test_pic_preallocation`.** The test
  runs the `vmaf_v0.6.1` model on 1080p frames in the unoptimised sanitizer
  build, about 9 s of CPU, and a hosted runner running the fast suite in
  parallel needs more than meson's default 30 s for that. The test now has an
  explicit `timeout : 180`, as `test_speed_filter` has; its cases are
  unchanged.
