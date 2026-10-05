- **A `-Denable_float=false` build scores with the default model again
  (port of Netflix/vmaf `6046b1926` and the build hunk of `4718b4f5f`).** The
  default model `vmaf_v1.0.16_3d0h` reads `speed_chroma`, but `speed_chroma`
  and `speed_temporal` were compiled and registered only with
  `enable_float=true`, so such a build stopped with `could not initialize
  feature extractor "Speed_chroma_feature_speed_chroma_uv_score"`. Both
  extractors are now part of every build, as in Netflix/vmaf, and a
  `-Denable_float=true` build (the default) is unchanged: every score is
  identical before and after.
