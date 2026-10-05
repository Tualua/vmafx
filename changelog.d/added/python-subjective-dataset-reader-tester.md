- **Python harness: `SubjectiveDatasetReader` and `SubjectiveDatasetTester`,
  and per-video sizes, resampling, `fps_cmd` and `workfile_yuv_type` in
  dataset files (port of Netflix/vmaf `2e6bbb657`, with the tests of
  `3685aa3c1` and `2f2bb601b`).** `read_dataset()` and
  `run_test_on_dataset()` are built on the two classes; the tester keeps the
  results and correlation stats of a run. A reference and a distorted video
  may now have different `width` / `height` (with `quality_width` /
  `quality_height` to meet at) and their own `resampling_type`; before, a
  size mismatch failed an assertion and a dataset file gave an asset one
  resampling type. Documented in `docs/usage/python.md`.
