- **`float_adm_hip` returns the CPU extractor's scores bit for bit**
  (ADR-1458). The HIP twin computed float ADM with eight differences from the
  CPU (the association of the angle test, partial sums per wave added in
  `double`, its own CSF weights, single-precision constants and gain limit,
  another order in the masking threshold, another floor) and matched it on
  224 of 1246 measured values, up to 1.3e-5 away. It now runs the CUDA twin's
  arithmetic from a shared header (`core/src/feature/float_adm_gpu_common.h`)
  and the CPU's own routines on the host. Measured on a gfx1036 at
  `--precision max`: 1246 of 1246 values identical, 3204 of 3204 with
  `debug=true`, and with five option sets. Stored `float_adm_hip` scores
  change by up to 1.3e-5. New: the option `adm_skip_aim_scale`, and frames
  below 17x17 are refused as on the CPU. No cost in frame time. The parity
  gate compares the cell at tolerance 0.
