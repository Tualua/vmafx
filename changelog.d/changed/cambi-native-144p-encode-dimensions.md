- **CAMBI accepts native 144p encode dimensions (port of Netflix/vmaf `4f3f71b68`).**
  The minimum of `enc_width` and `enc_height` is 144 (was 180 and 150), on the CPU
  extractor and on the CUDA, HIP, SYCL and Metal twins. See
  [CAMBI](docs/metrics/cambi.md).
