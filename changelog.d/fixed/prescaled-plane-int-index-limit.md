- **`float_vif` and SpEED refuse a prescaled plane past the `int` index of
  their resampling and filter code.** `core/src/feature/vif_tools.c` indexes
  a plane with `int`, so with `vif_prescale` or `speed_prescale` above about
  1.414 at the 32768x32768 picture cap the index overflowed and the
  resampling wrote outside the plane. `init()` now fails with `-EINVAL` and
  names the plane size. 16K (15360x8640) and every smaller picture are
  accepted at every prescale up to 4.0, as before, and no score changes.
