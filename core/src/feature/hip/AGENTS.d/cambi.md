---
paths:
  - core/src/feature/hip/integer_cambi_hip.c
  - core/src/feature/hip/integer_cambi_hip.h
  - core/src/feature/hip/integer_cambi/cambi_score.hip
invariant: CAMBI uses the shared TVI helper, CPU border rules, and device-resident single-wait execution.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# CAMBI: use the shared TVI helper and the CPU's border rules (ADR-1219)

Three exact-logic traps, all of which HIP twin fell into, together
collapsed its CAMBI score to **exactly 0.0** on banding content CPU
scores at 5.85.

1. **Call `vmaf_cambi_init_tvi_and_vlt()`; never re-derive TVI
   table.** Runs CPU's own bisection of
   `tvi_hard_threshold_condition` between `luma_range.foot` and
   `luma_range.head - diff - 1`, plus `vlt_luma` and derived-band
   validation. Two independent hand-ports (HIP and Metal) both
   searched *negated* predicate seeded from luma 0, giving
   `tvi_for_diff = [1026, 1025, 1024, 4]` against CPU's
   `[182, 309, 436, 563]`. Host-side scalar work done once in `init()`, so
   per-backend copy buys nothing.
2. **`cambi.c::filter_mode` leaves output rows 0 and `height-1`
   UNFILTERED.** Vertical writeback under `if (i > 1)`, covers rows
   `1 .. height-2`; horizontal results for border rows live only in
   3-row ring, never written back. Device: `cambi_hd_filter_v_pixel()`
   writes rows `1 .. height-2` only; rows 0 and `height-1` keep
   pre-filter value (still get level map).
3. **`get_spatial_mask_for_index()` ZERO-PADS its 7x7 box sum.**
   Summed-area table `memset` to zero, gated by
   `deriv_valid = (i < height)`, so out-of-frame tap adds nothing. Clamping taps to
   border pixel counts zero-derivative flag up to three extra times
   per axis, flips `box_sum > mask_index` on band of border pixels.

CAMBI parity fixture must band: CAMBI counts neighbour differences
of `1 .. num_diffs` (4 at default), so 8-bit gradient stepping 32
levels every 32 columns scores 0.0 on CPU too, makes assertion
`0 == 0`. Use 10-bit gradient of one level every two columns inside TVI
band (200..900), assert CPU score is non-degenerate first.

## CAMBI device-resident: no host stage, one wait (ADR-1378)

- Every `cambi.c` stage on device, ADR-1357 design. Per frame: one
  `vmaf_hip_picture_upload_staged()` of dist luma, memset of
  `CambiHipFrameState`, kernels of `integer_cambi/cambi_score.hip`, one
  88-byte `CambiHipResults` device-to-host copy. `collect()` = only wait.
  Never call `vmaf_cambi_preprocessing` / `_calculate_c_values` /
  `_spatial_pooling` / `_get_spatial_mask` / `_filter_mode` / `_decimate`
  from `integer_cambi_hip.c`; never sync stream mid-frame.
- Per-work-item math only in `integer_cambi/cambi_hip_device.h` (kernels +
  host replay compile same code). Kernels add decomposition, shared memory,
  barriers, atomics; nothing else.
- Parameter block laid out once by `cambi_hip_plan()` /
  `cambi_hip_plan_bind_scales()` (`integer_cambi_hip.h`, host, every build);
  replay calls same functions. Change layout there, not in extractor.
- Window, mask index, resize tables, contrast weights, reciprocal table,
  top-K mean, window guard = `cambi.c` helpers (`cambi_internal.h`:
  `vmaf_cambi_adjust_window`, `_mask_index`, `_resize_source_indices`,
  `_contrast_weights`, `_reciprocal_lut`, `_fixed_topk_mean`,
  `_check_window_fits_lut`). No local copies.
- `init()`: scaffold build -> `-ENOSYS` first, nothing else (ADR-1264);
  hipcc build -> host config + window guard before any device call. Guard:
  window^2 >= 4226 -> -EINVAL. SpEED twins same order (`-ENOSYS`, then
  `sc_configure()` / `st_configure()`).
- Top-K sum exact: fixed point 2^-24 (`CAMBI_HIP_FIXED_SHIFT` ==
  `VMAF_CAMBI_TOPK_FIXED_SHIFT`, static-asserted), 128-bit via hi/lo halves.
  Integer reductions only; no fp64; level band `compact < levels` (band top
  excluded, else histogram write lands in next chunk).
- Guards: `test_hip_cambi_device_math` (lockstep c-values replay with
  histogram canary, 12 banding fixtures, window cases),
  `test_hip_device_resident_contract.py` (planted regressions),
  `test_hip_cambi_parity` on device.
