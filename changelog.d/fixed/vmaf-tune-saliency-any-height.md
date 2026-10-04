- **`vmaf-tune` and `vmafx-tune-go recommend-saliency` accept frame heights that are
  not a multiple of 8.** Both tools refused them (a 576x324 clip included) before
  running the saliency model, although they already zero-pad the tensor to a multiple
  of 32 and crop the map back; the shipped `saliency_student_v1` was run at 576x324,
  8x8, 4x4 and 1x1 with that padding. The map has the frame's own shape. Frames that
  were accepted before score exactly as before
  ([ADR-1540](docs/adr/1540-saliency-pad-to-multiple-of-8.md) follow-up).
