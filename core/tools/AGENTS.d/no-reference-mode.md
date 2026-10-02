---
paths:
  - core/tools/vmaf.cpp
  - core/tools/cli_parse.cpp
invariant: --no-reference requires tiny_model_path, forces no_prediction; open_cli_inputs opens distorted source twice.
---
# No-reference scoring mode wiring

- [ADR-0520](../../../docs/adr/0520-cli-no-reference-wiring.md) —
  `--no-reference` wiring.
  **CLI gate invariant**: reference-required gate at end of
  `cli_parse()` must remain conditional on `!settings->no_reference`;
  NR branch must require `tiny_model_path`, force
  `no_prediction = true` so built-in `vmaf_v0.6.1` SVM is not
  auto-injected (SVM consumes FR feature columns, would always
  fail downstream). If `/sync-upstream` reintroduces unconditional
  `if (!settings->path_ref)` block, restore `no_reference` guard.
  **Frame-loop invariant**: in NR mode `vmaf.cpp::open_cli_inputs` opens
  distorted source twice (two `video_input` handles) so
  `vmaf_read_pictures` receives non-null picture pair; this
  satisfies public-API contract without exposing new entry
  point. Never collapse two opens into single handle —
  per-frame `vmaf_picture_unref` cleanup walks both slots
  independently, single-slot reuse would cause use-after-free.
  Rank-4 DNN dispatch in `libvmaf.c::vmaf_ctx_dnn_run_frame_nchw`
  reads picture data exclusively from `ref` argument, is
  *only* downstream consumer that legitimately observes that slot in
  NR mode.
