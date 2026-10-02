---
paths:
  - core/test/test_pixel_format_edge_coverage.c
  - core/test/test_ms_ssim_decimate.c
  - core/test/test_ms_ssim_decimate_coverage.c
invariant: test_pixel_format_edge_coverage.c owns cross-cutting smoke tests; MS-SSIM fixture dims must be >= 176x176.
---
<!-- markdownlint-disable MD013 -->
# Pixel-format edge coverage and fixture dimensions

- **MS-SSIM / `float_ms_ssim` fixture dims must be ≥ 176×176.**
  5-level 11-tap MS-SSIM pyramid rejects any input where
  `min(w, h) < GAUSSIAN_LEN
  << (SCALES - 1) = 11 << 4 = 176` at init with `-EINVAL` (see
  `core/src/feature/float_ms_ssim.c:131-138`,
  Netflix#1414 / ADR-0153). Test fixture below this floor will fail
  at *first* `vmaf_read_pictures` call with
  `"vmaf_read_pictures failed"`, masking actual code path you
  intended to test. Use 192×192 or larger (192 = 176 rounded up to
  multiple of 16 for clean pyramid downsamples). This caught
  `test_metal_float_ms_ssim_parity` on all macOS jobs at master
  `4948b771c`; see
  [ADR-0973](../../../docs/adr/0973-master-ci-regressions-verified-2026-05-31.md).
  **Rebase-sensitive**: any new test that exercises `float_ms_ssim` /
  `float_ms_ssim_metal` / `float_ms_ssim_*` (any backend) must use
  fixtures ≥ 176 in both dimensions.

## Pixel-format edge coverage invariant (ADR-0912)

`test_pixel_format_edge_coverage.c` is canonical home for
cross-cutting `(extractor × pix_fmt × bpc)` smoke tests. Five cases
ship today (PSNR on 4:2:2 8-bit, 4:4:4 10-bit, 4:2:0 12-bit; SSIM on
4:2:2 8-bit; CIEDE on 4:2:2 8-bit). When adding new extractor, or
extending existing one to previously-unsupported pixel format: add
follow-up case to this file rather than to new per-extractor file.
Audit value of one file per cross-cutting axis is higher than
per-extractor locality. File links only against **public** extractor
/ picture / collector C surface (no internal-source `#include`);
preserve that property so test stays regression gate for published
API. See
[ADR-0912](../../../docs/adr/0912-pixel-format-edge-coverage.md).
