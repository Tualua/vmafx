- **Metal: `ssimulacra2_metal` refuses 4:0:0 input instead of crashing
  (`T-METAL-SSIMULACRA2-YUV400-ACCEPTED-2026-10-05`).** The twin's `init()`
  discarded the pixel format, so a YUV400P picture reached the colour
  conversion, which read the missing U plane through a NULL pointer and killed
  the process. The Apple M4 Pro tester report of #2118 counted
  `test_metal_ssimulacra2_parity` failed for that reason while every case it
  printed passed. The twin now returns `-EINVAL` from `init()` with
  `ssimulacra2_metal: needs a YUV 4:2:0, 4:2:2 or 4:4:4 input, not 4:0:0`, as
  the CPU extractor and the CUDA, SYCL and HIP twins do. All five share one
  check (`core/src/feature/ssimulacra2_pixel_format.h`); the CUDA, SYCL and HIP
  refusals now end with `, not 4:0:0` like the CPU's. The `vmaf` CLI never
  produces 4:0:0 input; library callers of `vmaf_read_pictures()` were
  affected.
