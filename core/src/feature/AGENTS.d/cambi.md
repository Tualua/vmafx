---
paths:
  - core/src/feature/cambi.c
  - core/src/feature/cambi.h
invariant: CAMBI bounded searches, c-values window boundaries, and UTF-8 heatmap paths.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# CAMBI Searches, Window Boundaries, and Heatmap Paths

- **CAMBI bounded searches and live private helpers** (ADR-0205 / ADR-1146):
  `cambi.c` is strict-clean: it contains no `NOLINT` or Cppcheck suppression.
  Preserve the 16-step TVI bisection, the `UINT16_MAX`-bounded VLT scan, and
  the `n`/partition-span bounds on quick-select without changing comparison,
  pivot, swap, or accumulation order. The shared extractor callback ABI stays
  mutable; `read_only_picture_view()` is the const-view adapter Cppcheck can
  verify. All ten helpers declared in `cambi_internal.h` must remain exercised
  by real CPU/reference paths as well as available to GPU twins; do not replace
  those calls with analyzer annotations. Keep the compact `CAMBI_OPTION`
  descriptors equivalent to the public option table. See
  [measured source and binary equivalence](../../../../docs/research/2043-cambi-production-lint-2026-09-08.md).
- **CAMBI c-values walks stay inside short and narrow frames**
  (Netflix/vmaf#1628, port of Netflix/vmaf#1629, plus the column bound):
  `calculate_c_values()` in `cambi.c`, `calculate_c_values_avx2()` in
  `x86/cambi_avx2.c` and `cambi_calculate_c_values_frame()` in
  `cambi_c_values_frame.h` (AVX2 scan, AVX-512, NEON) bound the first pass to
  `MIN(pad_size, height)` rows, the top edge to `MIN(pad_size + 1, height)` and
  start the bottom edge at `MAX(height - pad_size, 0)`. The scalar and
  AVX2-mirror walks also bound every first column loop to
  `MIN(pad_size, width)`; the shared SIMD walk only visits columns below
  `width`. CAMBI decimates in place, so columns past `width` hold a finer
  scale's pixels, not zeros, and reading them changes the score without any
  sanitizer noticing. Keep the three walks identical; an upstream sync that
  re-imports `calculate_c_values()` keeps both bounds (upstream has neither
  the column bound nor, until #1629 merges, the row bounds).
  `test_calculate_c_values_short_frame` and
  `test_calculate_c_values_narrow_frame` (`core/test/test_cambi.c`) fail on
  every driver that loses one. The Metal twin runs this walk on the host
  through `vmaf_cambi_calculate_c_values()`. The SYCL, CUDA and HIP twins
  compute the c-values on the device and do not call it; `cambi_hip` clips
  each window to the frame itself (`cambi_hd_cvals_begin()`,
  `hip/integer_cambi/cambi_hip_device.h`) and must keep matching these bounds.
- **CAMBI heatmap paths are UTF-8 on Windows** (ADR-1182):
  `mkdirp.cpp` must create each component through `vmaf_mkdir_utf8`, and
  `cambi.c::open_heatmaps` must open every `.gray` file through
  `vmaf_open_utf8`. Keep `test_open_heatmaps_utf8_path` as a production-seam
  regression; a helper-only path test does not protect this call-site wiring.
