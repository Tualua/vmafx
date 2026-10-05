- **Every SYCL extractor now runs on zero-copy `libvmaf_sycl` input.** The float
  extractors (`float_psnr`, `float_adm`, `float_vif`, `float_motion`), `ssim`,
  `float_ssim`, `float_ms_ssim`, `ciede`, `ssimulacra2` and `speed_chroma` /
  `speed_temporal` used to stage through host pictures and failed with
  `-ENOTSUP` on QSV / VA-API frames; they now read the library's shared device
  planes, on host-uploaded and zero-copy input alike, so the `vmaf_float_v0.6.1`
  model scores on zero-copy too. On an Arc A380 the FFmpeg harness
  (`scripts/test/zerocopy-e2e.sh --stage 3`) finds every value equal to the CPU
  at 8-bit NV12 and 10-bit P010. Only the Windows D3D11 import stays luma only
  (its chroma readers still fail with `needs chroma planes`), see
  [ADR-1766](docs/adr/1766-sycl-host-staging-to-shared-planes.md) and the
  [SYCL overview](docs/backends/sycl/overview.md).
