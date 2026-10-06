- The HIP kernels of integer VIF, float VIF, PSNR-HVS and SSIMULACRA 2, and the
  shared GPU headers `ordered_sum.h`, `adm_angle_flag.h`, `ff_math.h`,
  `ciede_ff_math.h`, `adm_cm_accumulator.h`, `integer_adm.h`,
  `float_adm_gpu_common.h` and `float_vif_gpu_common.h`, now pass the hip-lane
  clang-tidy profile with zero findings (ADR-1142). Scores are unchanged: the
  HIP parity tests of the touched twins compare them with the CPU extractor on
  gfx1036. `VifBufferHip.ref` and `.dis` are typed pointers instead of
  `uintptr_t`; the struct layout is the same.
