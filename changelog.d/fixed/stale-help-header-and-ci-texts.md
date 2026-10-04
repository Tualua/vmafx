- **Help strings, header comments, option descriptions and CI comments now say what the code does.**
  `vmaf --help` no longer shows `(default: auto)` for `--hip_device` and `--metal_device` (both
  are opt-in); `vmaf_vpl --help` names the real default model; the `VMAF_SYCL_NO_GRAPH`
  deprecation warning recommends `VMAF_SYCL_DISPATCH=<feature>:direct` (the old advice,
  `VMAF_SYCL_USE_GRAPH=false`, did nothing) and prints once; the headers `picture.h`,
  `libvmaf_mcp.h`, `libvmaf_hip.h` and `libvmaf_metal.h`, the HIP / Metal / MCP / TAD build
  options, the fuzz README and the CI comments are corrected
  ([CLI](docs/usage/cli.md), [pictures](docs/api/pictures.md), [env vars](docs/usage/env-vars.md)).
  FFmpeg patch impact: none.
