- **`cambi` with `full_ref=true` scores the right distorted picture at 10 bits
  when the source is larger than the picture.** With `src_width` /
  `src_height` above the input size, the CPU extractor converted the 10-bit
  distorted plane with one copy at the input's row stride into a working
  picture allocated at the source width, so every row after the first was
  shifted and `cambi` and `cambi_full_reference` were wrong (10-bit Sparks
  frame 0: `cambi` 0.0048 instead of 0.3734). The plane is now copied row by
  row, and `cambi` no longer depends on `full_ref` or the source size. 8-, 9-,
  12- and 16-bit input was not affected; the Metal twin, which runs the same
  conversion on the host, is fixed with it. Upstream Netflix/vmaf has the
  same code.
