- **`adm_hip` is bit-identical to the CPU `adm` extractor, and no longer
  returns garbage for the first frame of a second context.** The HIP twin
  rounded the ADM denominator once per thread where the CPU rounds once per
  row, derived a rounding shift on the device with an fp32 logarithm, and
  concluded each scale with host copies of the CPU's routines. On a gfx1036 a
  low-detail 576x324 frame was 4.0e-7 off in `integer_adm_scale3`, BBB
  3840x2160 up to 1.4e-7 in `integer_adm_scale0`, and a 962x13542 frame scored
  `integer_adm_scale0` 0.860 where the CPU scores 0.979. The twin now takes
  its CSF weights, border, shifts and score conclusion from the CPU's own
  routines and folds the denominator once per row: 21 fixture pairs from
  18x22 to 3840x2160 at 8 to 16 bits are identical at `--precision max`, the
  per-scale sums of `debug=true` included, with every option set tried.
  Separately, the twin cleared its accumulators ahead of each frame's upload,
  and on that device such a clear is lost in the first context of a process
  that needs larger planes than the contexts before it: a program scoring a
  256x144 clip and then a 3840x2160 clip through the library got `invalid ADM
  reduction` on the second. The clear now follows the upload. The parity gate compares the CPU and HIP `adm` cells
  with tolerance 0. Re-run any stored `adm_hip` output
  ([ADR-1423](docs/adr/1423-hip-adm-cpu-row-rounding.md),
  [HIP backend](docs/backends/hip/overview.md#adm_hip-returns-the-cpus-values-bit-for-bit-2026-10-01)).
