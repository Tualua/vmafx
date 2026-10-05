- **`adm_cuda` and `adm_hip` take the CPU's integer reciprocal in the scale-0
  decouple.** The twins computed `2^30 / o` as an fp32 quotient truncated to an
  integer; the CPU reads `div_lookup`, the integer quotient. They differ for 343
  of the 32767 positive operands, and for a reference coefficient above 16566
  the restored sample differs too (318294 pairs at the mismatching operands).
  Real video does not reach it: the Netflix pair, both 1080p checkerboards and
  BBB 3840x2160 were identical before and after (0 difference at
  `--precision max`). A frame with isolated sign-aligned patches of full-scale
  detail did: `integer_adm_scale0` differed by 1.4e-6 on the RTX 4090 and the
  gfx1036. `adm_recip_q30()` (`adm_decouple_inline.cuh` and `.hip`) now returns the
  CPU's `div_lookup` value for every `int16` operand, from an fp32 estimate
  and an exact fp32 correction; an integer division was measured and refused
  (it takes `adm_cm_line_kernel_8` from 148 to 228 registers). Frame time at
  3840x2160 does not change measurably (CUDA 4.96 to 5.12 ms, HIP 263 to 261 ms
  on a loaded host).
