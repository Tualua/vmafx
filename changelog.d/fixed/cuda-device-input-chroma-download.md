- **A CPU extractor scores all three planes of device-resident input.** With
  the pictures in device memory (the FFmpeg `libvmaf_cuda` path, or the
  `DEVICE` picture preallocation) and an extractor that runs on the CPU,
  libvmaf downloaded the luma plane into the host picture only. The chroma
  planes stayed uninitialised: `psnr_cb` and `psnr_cr` came out as the 60 dB
  cap (the CPU gives 12.54 dB on the test pictures) and no error was reported.
  The download now takes every plane the picture has (Netflix/vmaf#1613).
