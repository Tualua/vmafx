- **The UBSan and TSan jobs finish `test_integer_psnr_coverage` and
  `test_metal_psnr_hvs_math`.** The APSNR wrap case needs 4.3e9 samples by
  construction and ran into the default 30 s timeout in the debug sanitizer
  builds; it now has a timeout sized from measurement. The Metal psnr_hvs math
  test formed the same per-block terms twice per masking table and now forms
  them once, which halves its run time with an identical report.
