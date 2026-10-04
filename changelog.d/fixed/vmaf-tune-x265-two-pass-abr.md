- **`vmaf-tune corpus --two-pass` encodes with `libx265`.** x265 refuses
  `-crf` in the second pass (exit 183), so every libx265 two-pass cell failed.
  A cell at a CRF now runs pass 1 at that CRF, measures the bitstream with
  `ffprobe` and runs pass 2 as ABR at that bitrate; the corpus row's
  `extra_params` ends with `-b:v <kbps>k` and its `crf` stays the pass-1 CRF, so
  a reader can tell the row belongs to an ABR encode. A missing bit rate fails
  the cell rather than guessing one. The Go `vmafx-tune-go` does the same
  (ADR-1565).
