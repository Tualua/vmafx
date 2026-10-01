- **`float_vif_hip` is bit-identical to the CPU `float_vif` extractor, and
  no longer faults on small frames.** The HIP twin filtered with a table of
  Gaussian taps the CPU stopped using, called the device `log2f()` where the
  CPU evaluates a polynomial, took the noise variance as a `float` where the
  CPU keeps a `double`, and added per wave and per block where the CPU adds
  row by row. On a gfx1036 10 of 712 scores (four scales, 178 frames from
  480x270 to 3840x2160 at 8 to 16 bits) were the CPU's; the others were up to
  3.8e-5 away on typical content and 1.06e-4 on bright 16-bit content, more
  than the twin's 5e-5 gate tolerance. The twin now runs the arithmetic of
  the CUDA twin from one shared header (`float_vif_gpu_common.h`) and all 712
  scores are identical at `--precision max`, with `debug=true` and the
  feature options too. It gains the CPU's `vif_scale1_min_val`,
  `vif_scale2_min_val` and `vif_scale3_min_val`. Frames smaller than 72
  pixels in either dimension, which ended with a GPU memory fault, now run.
  The parity gate compares the CPU and HIP `float_vif` cells with tolerance
  0. A frame takes 26.0 ms instead of 20.7 at 1920x1080 and 147 ms instead of
  86 at 3840x2160 on that device. Stored `float_vif_hip` scores change by up
  to 3.8e-5 ([ADR-1444](docs/adr/1444-hip-float-vif-cpu-arithmetic.md),
  [HIP backend](docs/backends/hip/overview.md#float_vif_hip-returns-the-cpus-scores-bit-for-bit-2026-10-02)).
