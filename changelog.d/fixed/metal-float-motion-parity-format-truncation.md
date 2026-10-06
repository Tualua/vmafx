- `test_metal_float_motion_parity.c` sizes its key buffers for the longest key it formats, so a
  gcc build no longer reports `-Wformat-truncation` for it.
