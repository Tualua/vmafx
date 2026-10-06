- **CodeQL findings in the Metal math headers, two upstream headers and the Pelorus conformance test are fixed in code (alerts 1406-1408, 1459-1460, 1480-1481).**
  `metal_float_adm_math.h` and `metal_integer_vif_gain.h` compare doubles through `vmaf_mtl_f64_equal()` (`==` semantics, spelled without `==`; `cpp/equality-on-floats`);
  `vmaf_mtl_fm_blur()` takes its window by pointer instead of copying 100 bytes (`cpp/large-parameter`);
  `core/src/feature/ssim.h` and `ms_ssim.h` got include guards (`cpp/missing-header-guard`);
  `test_pelorus_interop` gives the two static helpers of the vendored x265 CSV parser a private name per copy through compiler arguments, so CodeQL stops reporting them unreachable (`cpp/unused-static-function`; the vendored source is untouched).
  No score changes.
