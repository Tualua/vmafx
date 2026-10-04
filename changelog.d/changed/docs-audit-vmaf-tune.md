- Rebuilt the `vmaf-tune` documentation against the argparse code. The overview
  is a short page with a subcommand table that links every topic page; new pages
  cover `corpus`, `predict`, `report`, `auto`, `compare`, `benchmark`,
  `tune-per-shot`, the work directory, prefiltering, multi-pass and the codec
  adapter families. Corrected: the ladder CRF sweep (`20,25,30,35,40`), the 4K
  model (`vmaf_v1.0.16_1d5h_2160`), `--duration-frames`, `--output`, single-value
  `corpus --encoder`, the bisect default model, and the cache (Python API only,
  off by default; the documented cache flags never existed).
