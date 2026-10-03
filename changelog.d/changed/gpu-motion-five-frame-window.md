- **The CUDA, SYCL and HIP motion twins compute `motion_five_frame_window`.**
  `motion_cuda`, `motion_sycl`, `motion_hip` and the three `motion_v2` twins
  keep the frame two back on the device and derive `motion2` / `motion3` with
  the CPU's own window function, so a GPU run of a `vmaf_v1.0.16_hfr_*`
  model keeps its motion feature on the device. Every output equals the
  CPU's bit for bit on an RTX 4090, an Arc A380 and a gfx1036, and the parity
  gate has two exact cells for it, `motion_mffw` and `motion_v2_mffw`. With
  the option a twin publishes `motion2` and `motion3` at the end of the run,
  as the CPU does. `motion_metal` and `motion_v2_metal` leave the option to
  the CPU extractor. See
  [Motion, five-frame window](docs/metrics/motion.md#five-frame-window) and
  [ADR-1491](docs/adr/1491-gpu-motion-five-frame-window.md).
