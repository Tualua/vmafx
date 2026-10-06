- **The CUDA and HIP scale-0 ADM angle flag equals the CPU's at every int16 corner (`T-GPU-ADM-ANGLE-FLAG-S0-INT32-CORNER-2026-10-06`, ADR-2134).**
  `decouple_angle_flag_s0()` summed int16 products in int32, which wraps when every band is -32768 (17 of 256 corner combinations gave another
  flag than the CPU's, and with it another gain-limited decouple branch). It now sums in unsigned 32 bits restored to int64, as the CPU's int64 sums.
  The form costs `adm_cm_aim_line_kernel_4` 209 registers (ADR-1226's budget was 208; plain int64 is 216), so that kernel has its own budget in
  `test_cuda_adm_cm_register_pressure`; the other kernels keep 208 and spill is zero. An RC7 row wins the register back.
