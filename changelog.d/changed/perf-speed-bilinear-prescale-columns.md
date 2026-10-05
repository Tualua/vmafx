- **SpEED and `float_vif` with a bilinear prescale compute the column table
  once (port of Netflix/vmaf `78e11b52c`).** The source columns and weights
  of bilinear scaling depend only on the output column, so `speed_chroma` and
  `speed_temporal` compute them once per extractor instance and `float_vif`
  once per frame instead of once per pixel. Scores are bit-identical. With
  `speed_prescale_method=bilinear` (the `vmaf_v1.0.16_3d0h_2160` and
  `vmaf_v1.0.16_5d0h` models) `speed_chroma` alone took 4.8 ms per 3840x2160
  frame instead of 11.2 (one thread, median of three). Unlike upstream, which
  keeps the table on the stack and refuses bilinear outputs wider than 7680,
  the fork has no width limit.
