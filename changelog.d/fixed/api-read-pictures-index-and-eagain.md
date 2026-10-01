- **`libvmaf.h` and the API guide say what an index gap and an early query do.**
  `vmaf_read_pictures()` has always rejected a repeated or earlier index with
  `-EINVAL`; it also accepts an index that skips values, and then the motion
  extractors write no `motion2` / `motion3` for the pictures that follow, so
  reading them returns `-EAGAIN` even after the flush. A score asked for
  before the flush returns the value or `-EAGAIN`, never a partial value. The
  Doxygen of `vmaf_read_pictures()`, `vmaf_score_at_index()`,
  `vmaf_feature_score_at_index()` and the two pooled calls now carry both
  rules, and [the API guide](docs/api/index.md#scoring-before-the-flush-and-index-gaps)
  has a section on them (ADR-1429, Netflix/vmaf#910, #755, #1180). No
  behaviour changes.
