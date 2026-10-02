- **The `Cppcheck` check passes again.** The first complete hosted run on
  master since 2026-09-30 reported three findings (cppcheck 2.19.0,
  `--check-level=exhaustive`): `identicalInnerCondition` in
  `core/src/dict.cpp` (`if (*dict) return *dict;`) and
  `returnDanglingLifetime` twice in
  `core/test/test_video_input_odd_dims.c`, where a frame reader called
  through a function pointer was taken for an aggregate that keeps the
  address of a local. A later cleanup of `core/src/feature/adm.c` added
  eleven `invalidPointerCast` reports: the band planes were carved from a
  `char *` cursor with a direct `(float *)` cast. The cursor now has the
  sample type. No behaviour changes, and nothing is suppressed.
