- **SYCL: `speed_chroma` and `speed_temporal` no longer differ from the CPU by
  one fp32 step on a cancelling covariance entry.** The device summed each
  covariance term `(x - mean_x) * (y - mean_y)` near-exactly in parallel and
  rounded once; `speed.c` adds the fp64 terms one after the other and rounds
  every add, so on an entry whose terms cancel the two stored neighbouring fp32
  values. On frame 140 of a 3840x1600 10-bit segment scored with
  `vmaf_v1.0.16_3d0h` that moved `speed_chroma_u_mxv_45_nnf_0.1_snn_0.19_wvm_5`
  by 2.384e-07 (host-upload and QSV zero-copy alike). The twin now performs the
  reference's fp64 operations in the reference's order, one work-item per
  entry, and the 200-frame segment scores identical to the CPU. Costs about
  +10 ms per 3840x1600 frame on an Arc A380 until the optimised exact kernel
  lands (`T-SPEED-CHROMA-SYCL-COV-1ULP-2026-10-06`,
  `T-SYCL-SPEED-COV-EXACT-SEQUENTIAL-COST-2026-10-06`). The CUDA and HIP twins
  share the old design and are not verified.
