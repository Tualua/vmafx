- **`vmaf-tune`'s corpus, compare, encode and ladder modules meet the HISS-04
  size limits.** The 13 functions over 60 lines (among them `iter_rows` at 428
  lines) are split into helpers without a change in behaviour: the same rows,
  argv, subprocess order, errors and messages, checked against master by the
  test suite and by differential runs of the old and new modules. The HISS
  baseline loses those 13 infractions.
