- **`--feature mobilesal` scores frames whose sides are not multiples of 8
  with the saliency students.** `saliency_student_v1` and `v2` need both
  sides divisible by 8; a 576x324 clip failed inside ONNX Runtime with a
  `Concat` dimension mismatch. The extractor now pads the frame to the next
  multiple of 8 by repeating its last column and row and averages the saliency
  map over the frame's own area
  ([ADR-1540](docs/adr/1540-saliency-pad-to-multiple-of-8.md)). Frames that
  already are multiples of 8 score exactly as before.
