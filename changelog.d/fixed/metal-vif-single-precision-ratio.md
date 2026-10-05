- **`integer_vif_metal` divides each scale's sums in single precision, as the
  CPU does.** The CPU `vif` extractor stores each scale's numerator and
  denominator in a `float` and returns their quotient in single precision.
  The Metal twin rounded the two sums the same way but divided them in
  `double`, so every `VMAF_integer_feature_vif_scale0..3_score` of every
  frame differed from the CPU's: an outside tester's Apple M4 Pro measured up
  to 3.0e-8 on the Netflix 576x324 pair (192 of 192 scores), 1.5e-8 on the
  1080p 1-pixel checkerboard pair and 2.7e-8 at 10 bits. The integer sums
  were already the CPU's; only the final division changes. Stored
  `integer_vif_metal` scale scores change by up to half a `float` step; the
  debug sums and the `integer_vif` frame ratio do not change.
